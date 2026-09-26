//=============================================================================
// 15: mem2reg, the SSA construction. alloca+load/store is memory, SSA
// registers are plain values; this moves every stack slot into the value
// world and puts phis wherever control flow merges. Cytron et al. '91,
// and still what production mem2reg does:
//
//   phi placement: iterated dominance frontier of the def blocks. a phi
//     is itself a new def, which pushes more phis into its own frontier;
//     the worklist gets that "iterated" part for free.
//   renaming: preorder walk over the domtree with a per-slot stack of
//     "the value currently reaching here", pop on the way back up.
//     preorder is what makes it correct: everything dominating this
//     block has already pushed its def.
//   cleanup: a phi whose incomings collapsed to a single value is
//     trivial, fold it. iterate, because folding one can trivialize
//     another (Briggs et al).
//
// Why the frontier is the right answer: a def reaches everything it
// dominates for free, so a phi is only needed where that reach stops,
// and that is the dominance frontier by definition.
//
// Skipped on purpose: liveness pruning of phi candidates (I insert some
// phis a live-variable analysis would skip, then fold the trivial ones),
// the single-def fast path, and an explicit-stack rename walk (recursive
// here, fine for sane CFGs). A load with no reaching def becomes undef,
// which is what an uninitialized read is anyway.
//
// diff against the real one:
//   opt -load-pass-plugin .../LiteMem2Reg.so -passes=mem2reg-lite -S examples/mem2reg.ll
//   opt -passes=mem2reg -S examples/mem2reg.ll
//=============================================================================

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Plugins/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace {

class Promoter {
public:
  Promoter(Function &F, DominatorTree &DT) : F(F), DT(DT) {}

  bool run() {
    computeDF();
    collectSlots();
    for (Slot &S : Slots)
      insertPhis(S);
    if (DomTreeNode *Root = DT.getRootNode())
      rename(Root->getBlock());
    patchUnreachableEdges();
    foldTrivialPhis();
    cleanup();
    return !Slots.empty();
  }

  unsigned NumPhis = 0, NumLoads = 0, NumTrivialPhis = 0;

private:
  // Everything the pass knows about one stack slot.
  struct Slot {
    AllocaInst *A = nullptr;
    SmallPtrSet<BasicBlock *, 8> DefBlocks;
    SmallVector<StoreInst *, 8> Stores;
    SmallVector<LoadInst *, 8> Loads;
    DenseMap<BasicBlock *, PHINode *> Phis;
    SmallVector<Value *, 8> DefStack; // reaching values while renaming
  };

  Function &F;
  DominatorTree &DT;
  SmallVector<Slot, 8> Slots;
  DenseMap<AllocaInst *, unsigned> SlotIdx;
  DenseMap<BasicBlock *, SmallPtrSet<BasicBlock *, 8>> DF;
  SmallPtrSet<Instruction *, 32> Erased; // already deleted during rename

  static constexpr unsigned NoSlot = ~0u;

  unsigned storeSlot(StoreInst *SI) {
    auto *A = dyn_cast<AllocaInst>(SI->getPointerOperand()->stripPointerCasts());
    if (!A)
      return NoSlot;
    auto It = SlotIdx.find(A);
    return It == SlotIdx.end() ? NoSlot : It->second;
  }

  unsigned loadSlot(LoadInst *LI) {
    auto *A = dyn_cast<AllocaInst>(LI->getPointerOperand()->stripPointerCasts());
    if (!A)
      return NoSlot;
    auto It = SlotIdx.find(A);
    return It == SlotIdx.end() ? NoSlot : It->second;
  }

  // Dominance frontier, computed straight off the dominator tree (no
  // separate analysis needed): for every join block (>= 2 preds), climb
  // from each predecessor up the idom chain. Every block you pass through
  // on that climb has this join in its frontier.
  void computeDF() {
    for (BasicBlock &BB : F) {
      if (pred_size(&BB) < 2 || !DT.isReachableFromEntry(&BB))
        continue;
      BasicBlock *IDomBB = DT.getNode(&BB)->getIDom()->getBlock();
      for (BasicBlock *P : predecessors(&BB)) {
        if (!DT.isReachableFromEntry(P))
          continue;
        BasicBlock *Runner = P;
        while (Runner != IDomBB) {
          DF[Runner].insert(&BB);
          Runner = DT.getNode(Runner)->getIDom()->getBlock();
        }
      }
    }
  }

  void collectSlots() {
    // Entry-block allocas only: anything else is a dynamic alloca and
    // stays in memory.
    for (Instruction &I : F.getEntryBlock()) {
      auto *A = dyn_cast<AllocaInst>(&I);
      if (!A)
        continue;

      // The slot only gets promoted if its address never leaves the
      // load/store pattern. A gep, a call argument, or storing the pointer
      // itself somewhere all mean "the address escaped" -> leave it alone.
      // Volatile accesses are also off limits: the access itself is the
      // observable thing there, and the builtin refuses to promote them
      // too (atomics it does promote, so those stay allowed).
      bool Escaped = false;
      for (User *U : A->users()) {
        auto *UI = dyn_cast<Instruction>(U);
        if (auto *SI = dyn_cast_or_null<StoreInst>(UI)) {
          if (SI->isVolatile() || SI->getValueOperand() == A ||
              SI->getPointerOperand()->stripPointerCasts() != A)
            Escaped = true;
        } else if (auto *LI = dyn_cast_or_null<LoadInst>(UI)) {
          if (LI->isVolatile() ||
              LI->getPointerOperand()->stripPointerCasts() != A)
            Escaped = true;
        } else {
          Escaped = true;
        }
        if (Escaped)
          break;
      }
      if (Escaped)
        continue;

      SlotIdx[A] = Slots.size();
      Slot &S = Slots.emplace_back();
      S.A = A;
      for (User *U : A->users()) {
        auto *UI = cast<Instruction>(U);
        if (auto *SI = dyn_cast<StoreInst>(UI)) {
          S.Stores.push_back(SI);
          S.DefBlocks.insert(SI->getParent());
        } else {
          S.Loads.push_back(cast<LoadInst>(UI));
        }
      }
    }
  }

  // Iterated dominance frontier: start from the def blocks. Placing a phi
  // in a frontier block creates a *new* definition, which can push more
  // phis into its own frontier -- that's the "iterated" part, and the
  // worklist handles it for free.
  void insertPhis(Slot &S) {
    SmallVector<BasicBlock *, 16> Worklist(S.DefBlocks.begin(),
                                           S.DefBlocks.end());
    SmallPtrSet<BasicBlock *, 16> InWorklist(S.DefBlocks.begin(),
                                             S.DefBlocks.end());
    while (!Worklist.empty()) {
      BasicBlock *X = Worklist.pop_back_val();
      auto DFI = DF.find(X);
      if (DFI == DF.end())
        continue;
      for (BasicBlock *Y : DFI->second) {
        if (S.Phis.count(Y))
          continue;
        PHINode *P = PHINode::Create(S.A->getAllocatedType(), pred_size(Y),
                                     Twine(S.A->getName()) + ".phi",
                                     Y->getFirstInsertionPt());
        S.Phis[Y] = P;
        ++NumPhis;
        if (InWorklist.insert(Y).second)
          Worklist.push_back(Y);
      }
    }
  }

  // The rename walk. DefStack[S] holds the value that currently reaches
  // the point we're at. Pre-order over the domtree is exactly what makes
  // this correct: when we visit a block, everything that dominates it has
  // already pushed its definition.
  void rename(BasicBlock *BB) {
    SmallVector<Slot *, 8> PushedHere;

    for (Slot &S : Slots) {
      auto PI = S.Phis.find(BB);
      if (PI != S.Phis.end()) {
        S.DefStack.push_back(PI->second);
        PushedHere.push_back(&S);
      }
    }

    for (Instruction &I : make_early_inc_range(*BB)) {
      if (auto *SI = dyn_cast<StoreInst>(&I)) {
        unsigned Idx = storeSlot(SI);
        if (Idx == NoSlot)
          continue;
        Slot &S = Slots[Idx];
        S.DefStack.push_back(SI->getValueOperand());
        PushedHere.push_back(&S);
        Erased.insert(SI);
        SI->eraseFromParent();
      } else if (auto *LI = dyn_cast<LoadInst>(&I)) {
        unsigned Idx = loadSlot(LI);
        if (Idx == NoSlot)
          continue;
        Slot &S = Slots[Idx];
        Value *Cur = S.DefStack.empty()
                         ? UndefValue::get(S.A->getAllocatedType())
                         : S.DefStack.back();
        outs() << "  [mem2reg] " << I << "  ->  " << *Cur << "\n";
        LI->replaceAllUsesWith(Cur);
        Erased.insert(LI);
        LI->eraseFromParent();
        ++NumLoads;
      }
    }

    // Every edge out of this block owes the successor's phis an incoming
    // value: the current value, or undef if nothing reached us on this path.
    for (BasicBlock *Succ : successors(BB))
      for (Slot &S : Slots) {
        auto PI = S.Phis.find(Succ);
        if (PI == S.Phis.end())
          continue;
        PI->second->addIncoming(
            S.DefStack.empty() ? UndefValue::get(S.A->getAllocatedType())
                               : S.DefStack.back(),
            BB);
      }

    for (DomTreeNode *Child : DT.getNode(BB)->children())
      rename(Child->getBlock());

    // Leaving this subtree: undo everything this block defined.
    for (Slot *S : llvm::reverse(PushedHere))
      S->DefStack.pop_back();
  }

  // A phi owes one incoming value per predecessor *edge*. The rename walk
  // only visits blocks reachable from the entry, so if some unreachable
  // block still counts as a predecessor, its edges are missing here.
  void patchUnreachableEdges() {
    for (Slot &S : Slots)
      for (auto &KV : S.Phis) {
        PHINode *P = KV.second;
        if (P->getNumIncomingValues() >= pred_size(KV.first))
          continue;
        SmallPtrSet<BasicBlock *, 8> Covered;
        for (unsigned i = 0; i < P->getNumIncomingValues(); ++i)
          Covered.insert(P->getIncomingBlock(i));
        for (BasicBlock *Pred : predecessors(KV.first))
          if (!Covered.count(Pred))
            P->addIncoming(UndefValue::get(S.A->getAllocatedType()), Pred);
      }
  }

  // A phi whose non-undef incomings are all the same value V is a trivial
  // phi: every use might as well read V directly. undef counts as "any
  // value is fine", so a mix of V and undef is still trivial (that's the
  // one-def-branch case: the other path read an uninitialized slot).
  // A self-reference counts as "same" too -- it just means the slot kept
  // its value across that edge. Folding one phi can trivialize another
  // (phi of a folded phi), so iterate until nothing changes.
  void foldTrivialPhis() {
    bool FoldedAny = true;
    while (FoldedAny) {
      FoldedAny = false;
      for (Slot &S : Slots) {
        for (auto &KV : S.Phis) {
          PHINode *P = KV.second;
          if (!P)
            continue;
          Value *V = nullptr;
          bool SawReal = false, Trivial = true;
          for (unsigned i = 0; i < P->getNumIncomingValues(); ++i) {
            Value *In = P->getIncomingValue(i);
            if (isa<UndefValue>(In) || In == P)
              continue;
            if (!SawReal) {
              V = In;
              SawReal = true;
            } else if (In != V) {
              Trivial = false;
              break;
            }
          }
          if (!Trivial || !SawReal)
            continue;
          outs() << "  [mem2reg] folding trivial phi " << *P << "  ->  "
                 << *V << "\n";
          P->replaceAllUsesWith(V);
          P->eraseFromParent();
          KV.second = nullptr;
          ++NumTrivialPhis;
          FoldedAny = true;
        }
      }
    }
  }

  // Whatever's left can only be in unreachable blocks (rename covered
  // everything reachable): undef for the loads, delete the rest, and then
  // the alloca itself has no users left.
  void cleanup() {
    for (Slot &S : Slots) {
      for (LoadInst *LI : S.Loads)
        if (!Erased.count(LI)) {
          LI->replaceAllUsesWith(UndefValue::get(S.A->getAllocatedType()));
          LI->eraseFromParent();
        }
      for (StoreInst *SI : S.Stores)
        if (!Erased.count(SI))
          SI->eraseFromParent();
      S.A->eraseFromParent();
    }
  }
};

class Mem2RegLitePass : public RequiredPassInfoMixin<Mem2RegLitePass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM) {
    DominatorTree &DT = AM.getResult<DominatorTreeAnalysis>(F);

    Promoter P(F, DT);
    bool Changed = P.run();
    outs() << "mem2reg-lite: promoted " << P.NumLoads << " load(s) through "
           << P.NumPhis << " new phi(s), folded " << P.NumTrivialPhis
           << " trivial phi(s) in '" << F.getName() << "'\n";

    // CFG edges didn't move (phis don't touch them), so the dominator tree
    // and loop info are still valid. Memory-based analyses are not -- say
    // so and let the PM recompute those.
    return Changed ? PreservedAnalyses::allInSet<CFGAnalyses>()
                   : PreservedAnalyses::all();
  }
};

} // end anonymous namespace

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "Mem2RegLite", LLVM_VERSION_STRING,
          [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                [](StringRef Name, FunctionPassManager &FPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                  if (Name != "mem2reg-lite")
                    return false;
                  FPM.addPass(Mem2RegLitePass());
                  return true;
                });
          }};
}

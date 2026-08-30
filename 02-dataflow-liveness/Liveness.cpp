//=============================================================================
// 02: liveness, the textbook way. gen/kill per block, backward equations,
// worklist until nothing changes. Writing it by hand once taught me why
// nobody writes dataflow by hand.
//
//   LIVE_OUT[B] = U LIVE_IN[S]   over successors S
//   LIVE_IN[B]  = USE[B] U (LIVE_OUT[B] - DEF[B])
//
// USE[B] = read in B before any def in B (upward-exposed). IR is SSA so
// plain registers never get killed, but allocas still behave like real
// variables, so they stay in the analysis. Closest thing to textbook
// liveness LLVM IR will give you.
//
// One non-obvious bit: reverse postorder on the reverse CFG makes the
// worklist converge in a couple of sweeps. Random block order also
// converges, just embarrassingly slowly.
//=============================================================================

#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Plugins/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"

#include <vector>

using namespace llvm;

namespace {

class LivenessAnalysis {
public:
  explicit LivenessAnalysis(Function &F) : F(F) { run(); }

  void print(raw_ostream &OS) const;

private:
  Function &F;
  DenseMap<BasicBlock *, unsigned> BlockIndex; // block -> bitvector index
  std::vector<BitVector> LiveIn, LiveOut;      // one bitvector per block
  std::vector<AllocaInst *> SlotVars;          // index -> stack slot
  DenseMap<const AllocaInst *, unsigned> SlotIdx;

  unsigned slot(const AllocaInst *A) { return SlotIdx.lookup(A); }

  void run() {
    // ---- step 1: collect all "trackable variables" = all allocas -------
    // Real production analyses usually run mem2reg first to get rid of
    // allocas and do liveness over SSA values; I keep them here on purpose.
    for (Instruction &I : F.getEntryBlock())
      if (auto *A = dyn_cast<AllocaInst>(&I)) {
        SlotIdx[A] = SlotVars.size();
        SlotVars.push_back(A);
      }
    unsigned N = SlotVars.size();

    // ---- step 2: number the blocks (reverse postorder) -----------------
    // Convergence speed depends on iteration order. The numbering is
    // forward RPO and the worklist is a stack, so blocks come off
    // back-to-front: successors before predecessors, which is exactly the
    // direction a backward analysis pushes information. An acyclic graph
    // reaches the fixpoint in (essentially) one sweep.
    ReversePostOrderTraversal<Function *> RPOT(&F);
    unsigned Idx = 0;
    for (BasicBlock *BB : RPOT)
      BlockIndex[BB] = Idx++;
    // RPOT only numbers blocks reachable from entry. Unreachable blocks
    // have no dataflow facts here -- leaving them out of every table is
    // fine, as long as nothing indexes by the raw block count.
    unsigned NumTracked = BlockIndex.size();

    LiveIn.assign(NumTracked, BitVector(N));
    LiveOut.assign(NumTracked, BitVector(N));

    // ---- step 3: compute each block's USE/DEF sets ---------------------
    // USE[B] (upward-exposed): slots read in B before being written in B.
    // DEF[B]: slots written by a store in B.
    std::vector<BitVector> Use(NumTracked, BitVector(N));
    std::vector<BitVector> Def(NumTracked, BitVector(N));
    for (BasicBlock &BB : F) {
      BitVector &U = Use[BlockIndex[&BB]];
      BitVector &D = Def[BlockIndex[&BB]];
      for (Instruction &I : BB) {
        if (auto *SI = dyn_cast<StoreInst>(&I)) {
          if (auto *A = dyn_cast<AllocaInst>(
                  SI->getPointerOperand()->stripPointerCasts()))
            if (SlotIdx.count(A)) {
              D.set(slot(A)); // a store is the kill: reads before it are USE
              continue;
            }
        }
        // Everything else: a load from a tracked slot (or anything else
        // using the slot's address) counts as a USE. I only catch loads
        // here -- the common case -- which is plenty for a teaching build.
        if (auto *LI = dyn_cast<LoadInst>(&I))
          if (auto *A = dyn_cast<AllocaInst>(
                  LI->getPointerOperand()->stripPointerCasts()))
            // If the slot was already written earlier in this block, the
            // read is not upward-exposed.
            if (SlotIdx.count(A) && !D.test(slot(A)))
              U.set(slot(A));
      }
    }

    // ---- step 4: worklist iteration to the fixpoint --------------------
    // Invariant: LiveIn/LiveOut only grow (BitVector |=, monotone). The
    // lattice has height 2 per dimension, so iteration must terminate --
    // that's the standard monotone framework convergence argument.
    std::vector<unsigned> Worklist(NumTracked);
    for (unsigned i = 0; i < NumTracked; ++i)
      Worklist[i] = i;
    while (!Worklist.empty()) {
      unsigned B = Worklist.back();
      Worklist.pop_back();

      // LIVE_OUT[B] = U LIVE_IN[S], S in succ(B)
      BitVector NewOut(N);
      BasicBlock *BB = nullptr;
      for (auto &KV : BlockIndex)
        if (KV.second == B) { BB = KV.first; break; }
      for (BasicBlock *Succ : successors(BB))
        NewOut |= LiveIn[BlockIndex[Succ]];

      // LIVE_IN[B] = USE[B] U (LIVE_OUT[B] - DEF[B])
      BitVector NewIn = NewOut;
      NewIn.reset(Def[B]);
      NewIn |= Use[B];

      if (NewIn != LiveIn[B] || NewOut != LiveOut[B]) {
        LiveIn[B] = NewIn;
        LiveOut[B] = NewOut;
        // Something changed -- push the predecessors so the change flows
        // in the backward direction.
        for (BasicBlock *Pred : predecessors(BB))
          Worklist.push_back(BlockIndex[Pred]);
      }
    }
  }
};

void LivenessAnalysis::print(raw_ostream &OS) const {
  OS << "==================== liveness: " << F.getName() << " ====================\n";
  auto name = [&](const BitVector &BV) {
    std::string S;
    bool First = true;
    for (unsigned i = 0; i < SlotVars.size(); ++i)
      if (BV.test(i)) {
        if (!First) S += " ";
        S += "%" + SlotVars[i]->getName().str();
        First = false;
      }
    return S.empty() ? "(none)" : S;
  };
  for (BasicBlock &BB : F) {
    auto It = BlockIndex.find(&BB);
    if (It == BlockIndex.end()) {
      // unreachable from entry: no facts computed (and RPOT never visited
      // it, so there are none to show)
      OS << "[" << BB.getName() << "]\n";
      OS << "  (unreachable from entry)\n";
      continue;
    }
    unsigned B = It->second;
    OS << "[" << BB.getName() << "]\n";
    OS << "  live-in : " << name(LiveIn[B]) << "\n";
    OS << "  live-out: " << name(LiveOut[B]) << "\n";
  }
}

class LivenessPass : public RequiredPassInfoMixin<LivenessPass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &) {
    LivenessAnalysis(F).print(outs());
    return PreservedAnalyses::all();
  }
};

} // end anonymous namespace

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "LivenessPass", LLVM_VERSION_STRING,
          [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                [](StringRef Name, FunctionPassManager &FPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                  if (Name != "liveness")
                    return false;
                  FPM.addPass(LivenessPass());
                  return true;
                });
          }};
}

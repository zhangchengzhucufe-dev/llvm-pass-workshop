//=============================================================================
// 17: GVN lite, dominance-based value numbering. Walk the domtree
// preorder with a table of "expression -> first value that computed it";
// a hit means a dominating leader, so replace. Expression = opcode +
// nuw/nsw/exact flags + operands, with commutative operands sorted so
// x+y and y+x hash the same. The flags stay in the key on purpose:
// add nsw can be poison on overflow and plain add cannot, so they are
// not the same value.
//
// The bug that shaped this pass: v1 shared one hash table across the
// whole walk, and it merged a use in `else` with a computation in its
// sibling `then`. Siblings do not dominate each other. So now the table
// is a stack of scopes: push one per block, pop at subtree exit -- the
// exit markers in the worklist are exactly that pop.
//
// Known gaps, all of the miss-not-crash kind: entries go stale when an
// operand gets replaced (real GVN rewrites leaders), floats and calls
// and loads are out of scope, and there is no PRE, which is the actually
// clever part of GVN.
//=============================================================================

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Plugins/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace {

// The "expression" two instructions have to agree on to be interchangeable.
// Opcode + wrap/exact flags + operands. RHS is null for casts.
struct ExprKey {
  unsigned Opcode = ~0u; // ~0u marks "not something we track"
  unsigned Flags = 0;    // bit 0: nuw, bit 1: nsw, bit 2: exact
  Value *LHS = nullptr;
  Value *RHS = nullptr;
};

struct ExprKeyInfo {
  // DenseMapInfo<T*> no longer carries empty/tombstone sentinels on main,
  // so roll our own: -1/-2 are reserved, real instructions never live there.
  static inline ExprKey getEmptyKey() {
    return {0, 0, reinterpret_cast<Value *>(static_cast<uintptr_t>(-1)), nullptr};
  }
  static inline ExprKey getTombstoneKey() {
    return {0, 0, reinterpret_cast<Value *>(static_cast<uintptr_t>(-2)), nullptr};
  }
  static unsigned getHashValue(const ExprKey &K) {
    return hash_combine(K.Opcode, K.Flags,
                        DenseMapInfo<Value *>::getHashValue(K.LHS),
                        DenseMapInfo<Value *>::getHashValue(K.RHS));
  }
  static bool isEqual(const ExprKey &A, const ExprKey &B) {
    return A.Opcode == B.Opcode && A.Flags == B.Flags && A.LHS == B.LHS &&
           A.RHS == B.RHS;
  }
};

// One scope = one block's worth of table entries. Lookup goes through the
// whole stack (innermost first); insertion always lands on the top scope.
using ExprTable = DenseMap<ExprKey, Value *, ExprKeyInfo>;

class GvnLitePass : public RequiredPassInfoMixin<GvnLitePass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM) {
    // The PM reuses this pass object for every function, so reset any
    // state from the previous one.
    Eliminated = 0;
    DominatorTree &DT = AM.getResult<DominatorTreeAnalysis>(F);

    // Explicit pre-order walk over the domtree. The exit markers are the
    // point: a scope has to stay alive for the whole subtree, and siblings
    // must never see each other's entries.
    struct WorkItem {
      DomTreeNode *Node;
      bool IsExit;
    };
    SmallVector<WorkItem, 16> Work;
    Work.push_back({DT.getRootNode(), false});

    while (!Work.empty()) {
      WorkItem WI = Work.pop_back_val();
      if (WI.IsExit) {
        Scopes.pop_back();
        continue;
      }
      Scopes.emplace_back();
      processBlock(*WI.Node->getBlock(), DT);
      Work.push_back({WI.Node, true});
      // child order is irrelevant to DVNT correctness (each block is
      // processed once, scopes keep siblings apart); children() on main
      // is a forward range anyway, so no llvm::reverse here.
      for (DomTreeNode *Child : WI.Node->children())
        Work.push_back({Child, false});
    }

    outs() << "gvn-lite: eliminated " << Eliminated
           << " redundant instruction(s) in '" << F.getName() << "'\n";
    return Eliminated ? PreservedAnalyses::none() : PreservedAnalyses::all();
  }

private:
  SmallVector<ExprTable, 8> Scopes;
  unsigned Eliminated = 0;

  Value *lookup(const ExprKey &K) {
    for (auto It = Scopes.rbegin(); It != Scopes.rend(); ++It) {
      auto Hit = It->find(K);
      if (Hit != It->end())
        return Hit->second;
    }
    return nullptr;
  }

  static bool isCommutativeOp(unsigned Op) {
    switch (Op) {
    case Instruction::Add:
    case Instruction::Mul:
    case Instruction::And:
    case Instruction::Or:
    case Instruction::Xor:
      return true;
    default:
      return false;
    }
  }

  // Build the key for one instruction, or return Opcode == ~0u when the
  // instruction is outside our (deliberately small) whitelist.
  static ExprKey keyFor(Instruction &I) {
    if (auto *BO = dyn_cast<BinaryOperator>(&I)) {
      if (!BO->getType()->isIntegerTy())
        return {};
      unsigned Flags = 0;
      if (auto *OB = dyn_cast<OverflowingBinaryOperator>(BO)) {
        if (OB->hasNoUnsignedWrap())
          Flags |= 1;
        if (OB->hasNoSignedWrap())
          Flags |= 2;
      }
      if (auto *PE = dyn_cast<PossiblyExactOperator>(BO))
        if (PE->isExact())
          Flags |= 4;
      Value *L = BO->getOperand(0), *R = BO->getOperand(1);
      if (isCommutativeOp(BO->getOpcode()) && L < R)
        std::swap(L, R); // canonical order: x+y and y+x hash the same
      return {BO->getOpcode(), Flags, L, R};
    }
    if (auto *CI = dyn_cast<CastInst>(&I)) {
      if (!CI->getType()->isIntegerTy() ||
          !CI->getOperand(0)->getType()->isIntegerTy())
        return {};
      return {CI->getOpcode(), 0, CI->getOperand(0), nullptr};
    }
    return {};
  }

  void processBlock(BasicBlock &BB, DominatorTree &DT) {
    for (auto It = BB.begin(); It != BB.end();) {
      Instruction &I = *It;
      ++It;

      ExprKey K = keyFor(I);
      if (K.Opcode == ~0u)
        continue;

      if (Value *Leader = lookup(K)) {
        // Anything found in an ancestor scope or earlier in this block
        // dominates I by construction of the walk, but the dominates()
        // check costs nothing and makes the invariant explicit.
        if (DT.dominates(Leader, &I)) {
          outs() << "  [gvn] " << I << "  ==  " << *Leader << "\n";
          I.replaceAllUsesWith(Leader);
          I.eraseFromParent();
          ++Eliminated;
          continue;
        }
      }
      Scopes.back()[K] = &I; // first occurrence wins, it becomes the leader
    }
  }
};

} // end anonymous namespace

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "GvnLite", LLVM_VERSION_STRING,
          [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                [](StringRef Name, FunctionPassManager &FPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                  if (Name != "gvn-lite")
                    return false;
                  FPM.addPass(GvnLitePass());
                  return true;
                });
          }};
}

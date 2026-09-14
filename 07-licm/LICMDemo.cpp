//=============================================================================
// 07: LICM, simplified. First pass here that moves IR around instead of
// only deleting it, which also means PreservedAnalyses has to be right
// afterwards.
//
// My invariant test: no side effects, every operand defined outside the
// loop or already proven invariant this round, nothing may-throw or
// volatile. The safety rule is the part that matters: after hoisting, the
// instruction no longer runs only-when-the-loop-runs.
// sdiv is the classic trap -- division by zero traps, and a loop that
// never executes never traps. Hoist it and you have added a crash the
// original program did not have. Hence isSafeToSpeculativelyExecute as a
// hard filter.
//
// before/after:
//   opt -load-pass-plugin .../LICMDemo.so -passes='function(licm-demo)' -S examples/licm.ll
//=============================================================================

#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/LoopInfo.h" // LoopInfo + the new PM's LoopAnalysis
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Plugins/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"

#include <vector>

using namespace llvm;

namespace {

class LICMDemoPass : public RequiredPassInfoMixin<LICMDemoPass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM) {
    LoopInfo &LI = AM.getResult<LoopAnalysis>(F);
    DominatorTree &DT = AM.getResult<DominatorTreeAnalysis>(F);
    AAResults &AA = AM.getResult<AAManager>(F);
    (void)AA; // TODO: when hoisting loads, use AA to prove
              // "nobody inside the loop writes this address"

    unsigned Hoisted = 0;
    // Top-level loops only -- production LICM walks inner-to-outer.
    for (Loop *L : LI) {
      if (!L->getParentLoop() && L->getLoopPreheader())
        Hoisted += hoistInvariant(L, DT);
    }

    outs() << "licm-demo: hoisted " << Hoisted
           << " instruction(s) out of loops in '" << F.getName() << "'\n";

    // We moved instructions: all value-dependent analyses are stale. The
    // CFG (block/edge structure) didn't change, so CFGAnalyses survives.
    return Hoisted ? PreservedAnalyses::allInSet<CFGAnalyses>()
                   : PreservedAnalyses::all();
  }

private:
  unsigned hoistInvariant(Loop *L, DominatorTree &DT) {
    BasicBlock *Preheader = L->getLoopPreheader();
    Instruction *InsertPt = Preheader->getTerminator();

    // "Defined outside the loop": the definition lives in no loop block.
    auto IsOutside = [&](Value *V) {
      auto *I = dyn_cast<Instruction>(V);
      return !I || !L->contains(I->getParent());
    };

    std::vector<Instruction *> Invariant;
    SmallPtrSet<Instruction *, 16> Marked;

    for (BasicBlock *BB : L->blocks())
      for (Instruction &I : *BB) {
        // (a) pure arithmetic/logic only, and it must be safe to
        //     speculatively execute -- div/srem can trap on a zero
        //     divisor, and if the loop never runs, that trap never
        //     happened in the original program. Hoisting would invent a
        //     new crash point, so those are excluded.
        auto *BO = dyn_cast<BinaryOperator>(&I);
        if (!BO || !isSafeToSpeculativelyExecute(BO))
          continue;

        // (b) all operands are "outside or already proven invariant".
        bool AllOK = true;
        for (Value *Op : I.operands())
          if (!IsOutside(Op) && !Marked.count(cast<Instruction>(Op))) {
            AllOK = false;
            break;
          }
        // The textbook also asks whether the uses are dominated properly --
        // not a problem here: a side-effect-free instruction computes the
        // same value whenever it runs, so computing it earlier is invisible.
        if (AllOK) {
          Invariant.push_back(&I);
          Marked.insert(&I);
        }
      }

    // Move them to the preheader in discovery order (dependency order is
    // already guaranteed by the Marked set).
    for (Instruction *I : Invariant) {
      outs() << "  [licm] hoisting: " << *I << "\n";
      I->moveBefore(InsertPt->getIterator());
    }
    return Invariant.size();
  }
};

} // end anonymous namespace

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "LICMDemo", LLVM_VERSION_STRING,
          [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                [](StringRef Name, FunctionPassManager &FPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                  if (Name != "licm-demo")
                    return false;
                  FPM.addPass(LICMDemoPass());
                  return true;
                });
          }};
}

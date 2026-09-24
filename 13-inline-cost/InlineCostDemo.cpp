//=============================================================================
// 13: inline cost model skeleton, analysis-only, never touches IR.
//
//   cost     = instruction count with weights (calls and memory ops cost
//              more)
//   bonus    = dead arguments from constant actuals -- pass a constant
//              in, the parameter's uses fold away and get DCE'd, so that
//              callsite is cheaper than it looks
//   decision = inline when cost - bonus <= threshold
//
// The real InlineCost.cpp is ~2000 lines and its benefit term includes
// what inlining unlocks downstream: constant argument -> fixed trip
// count -> full unroll. Counting raw instructions is the usual first
// attempt, and it always overestimates -- that's the whole reason the
// bonus term exists, even in this skeleton.
//
// opt -load-pass-plugin .../InlineCostDemo.so -passes=inline-cost -disable-output examples/inline.ll
//=============================================================================

#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Plugins/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace {

static cl::opt<int> InlineThreshold("inline-demo-threshold", cl::Hidden,
                                    cl::init(10));

static unsigned instCost(const Instruction &I) {
  // Weights are hand-picked to mimic "roughly how much machine code does
  // this turn into". Calls are the most expensive.
  if (isa<CallBase>(I))
    return 20;
  if (isa<LoadInst>(I) || isa<StoreInst>(I))
    return 3;
  if (isa<PHINode>(I))
    return 0; // phis don't generate machine code
  return 1;
}

class InlineCostDemoPass : public RequiredPassInfoMixin<InlineCostDemoPass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &) {
    for (BasicBlock &BB : F)
      for (Instruction &I : BB) {
        auto *CB = dyn_cast<CallBase>(&I);
        if (!CB || !CB->getCalledFunction() ||
            CB->getCalledFunction()->isDeclaration())
          continue;
        const Function &Callee = *CB->getCalledFunction();

        // ---- cost: the callee's approximate size ------------------------
        unsigned Cost = 0;
        for (auto &J : instructions(const_cast<Function &>(Callee)))
          Cost += instCost(J);

        // ---- benefit: constant arguments make parameters dead -----------
        // A constant parameter's uses fold away after inlining; credit 2
        // points per use (a stand-in for the instructions DCE would eat).
        unsigned Bonus = 0;
        unsigned ArgIdx = 0;
        for (const Argument &A : Callee.args()) {
          if (ArgIdx < CB->arg_size() && isa<ConstantInt>(CB->getArgOperand(ArgIdx)))
            Bonus += 2 * A.getNumUses();
          ++ArgIdx;
        }

        bool Should = (int)(Cost > Bonus ? Cost - Bonus : 0) <= InlineThreshold;
        outs() << "  call '" << Callee.getName() << "' at '" << BB.getName()
               << "': cost=" << Cost << ", const-arg bonus=" << Bonus
               << " -> " << (Should ? "inline" : "keep") << "\n";
      }
    return PreservedAnalyses::all();
  }
};

} // namespace

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "InlineCostDemo", LLVM_VERSION_STRING,
          [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                [](StringRef Name, FunctionPassManager &FPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                  if (Name != "inline-cost")
                    return false;
                  FPM.addPass(InlineCostDemoPass());
                  return true;
                });
          }};
}

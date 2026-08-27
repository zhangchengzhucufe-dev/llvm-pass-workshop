//=============================================================================
// 01: the hello pass. just the new-PM plugin skeleton: PassInfoMixin, a
// pipeline parsing callback so `opt -passes=hello` finds it, and
// registerOptimizerLastEPCallback so default<O2> picks it up too (it does
// hook in).
//
// Also pulls LoopInfo out of the AnalysisManager without touching any IR.
// The AM chains the dependencies itself (LoopInfo wants DomTreeAnalysis),
// which took me a minute to trust.
//
// PreservedAnalyses is on your honor. Read-only pass, so all() is correct
// here. Lie with all() after mutating IR and every pass after you runs on
// stale results -- the classic new-PM bug.
//
// opt -load-pass-plugin build/01-hello-pass/HelloPass.so -passes=hello -S examples/trivial.ll
//=============================================================================

#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Plugins/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "hello"

STATISTIC(HelloCounter, "Number of functions greeted");

namespace {

// HelloPass: prints basic stats for each function.
//
// The key thing is the template argument -- it's the "IR unit type". A
// function pass inherits from RequiredPassInfoMixin<HelloPass>, and run() has the
// signature
//   PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM)
// Module/loop passes work the same way, just swap the IR unit and its
// matching AnalysisManager.
class HelloPass : public RequiredPassInfoMixin<HelloPass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM) {
    ++HelloCounter;

    outs() << "==================== hello: " << F.getName() << " ====================\n";
    outs() << "  linkage: " << F.getLinkage() << "\n";

    // Count instructions statically. inst_begin/inst_end expands into a
    // walk over all BasicBlocks.
    unsigned NumInsts = 0, NumCalls = 0;
    for (Instruction &I : instructions(F)) {
      ++NumInsts;
      if (isa<CallBase>(I))
        ++NumCalls;
    }
    outs() << "  basic blocks: " << F.size()
           << ", instructions: " << NumInsts
           << ", calls: " << NumCalls << "\n";

    // Ask the AnalysisManager for LoopInfo. Note we don't modify any IR,
    // yet we still got a "result" out of an analysis -- analyses and
    // transforms are separate things in the new PM, that's the design.
    // LoopInfo itself depends on DominatorTreeAnalysis, and the AM builds
    // that chain for us.
    LoopInfo &LI = AM.getResult<LoopAnalysis>(F);
    unsigned NumLoops = 0;
    for (Loop *L : LI)
      (void)L, ++NumLoops;
    outs() << "  loops (top level): " << NumLoops << "\n";

    // Read-only, so every analysis stays valid and everything is preserved.
    // If you touch the IR and then lie with PreservedAnalyses::all(),
    // downstream passes run on stale data.
    return PreservedAnalyses::all();
  }

};

} // end anonymous namespace

// Plugin entry point. After opt dlopens this .so it calls this C symbol,
// and we register the pass with the (new PM) PassBuilder.
extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "HelloPass", LLVM_VERSION_STRING,
          [](PassBuilder &PB) {
            // 1) explicit pipeline syntax: opt -passes=hello
            PB.registerPipelineParsingCallback(
                [](StringRef Name, FunctionPassManager &FPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                  if (Name != "hello")
                    return false;
                  FPM.addPass(HelloPass());
                  return true;
                });

            // 2) auto-insert when -passes='default<O2>' is used (extend the
            //    O2 pipeline, append our pass after the vectorizer slot).
            PB.registerOptimizerLastEPCallback(
                [](ModulePassManager &MPM, OptimizationLevel Level,
                   ThinOrFullLTOPhase) {
                  if (Level == OptimizationLevel::O1 ||
                      Level == OptimizationLevel::O2 ||
                      Level == OptimizationLevel::O3)
                    MPM.addPass(
                        createModuleToFunctionPassAdaptor(HelloPass()));
                });
          }};
}

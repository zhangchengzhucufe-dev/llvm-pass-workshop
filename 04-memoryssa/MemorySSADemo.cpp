//=============================================================================
// 04: MemorySSA. Three node types:
//   MemoryPhi -- phi over the state of memory at block entry
//   MemoryUse -- a load
//   MemoryDef -- a store, or a call that might write
// A name in this graph is "the state of memory at this point" -- that one
// idea is what makes memory questions answerable at all.
//
// The query I end up using in 16 is getClobberingMemoryAccess: nearest
// upstream access that could affect me. 03's pairwise scan was O(n^2);
// this is close to O(1) per query.
//
// Best way to absorb this one: dump the graph next to the .ll and read
// them side by side, line by line.
//
// opt -load-pass-plugin .../MemorySSADemo.so -passes=memoryssa-demo -disable-output examples/trivial.ll
//=============================================================================

#include "llvm/Analysis/MemorySSA.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Plugins/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace {

class MemorySSADemoPass : public RequiredPassInfoMixin<MemorySSADemoPass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM) {
    // Ask for MemorySSA (it depends on AAManager and the DominatorTree;
    // the AM builds those for us automatically).
    MemorySSA &MSSA = AM.getResult<MemorySSAAnalysis>(F).getMSSA();

    outs() << "==================== memoryssa: " << F.getName()
           << " ====================\n";
    // Dump the whole structure. There's a def chain right at the start of
    // the `entry` block, and every write hangs off one unified "memory
    // version chain".
    MSSA.print(outs());

    // For every memory access, ask "who is my nearest clobber?".
    // The query needs a walker: getClobberingMemoryAccess goes through a
    // MemorySSAWalker (here the standard walk-up-the-chain one).
    MemorySSAWalker &Walker = *MSSA.getWalker();
    for (BasicBlock &BB : F)
      for (Instruction &I : BB) {
        MemoryAccess *MA = MSSA.getMemoryAccess(&I);
        if (!MA)
          continue; // instructions that don't touch memory (pure integer
                    // arithmetic, say) have no MSSA node

        if (auto *MU = dyn_cast<MemoryUse>(MA)) {
          // A read: the clobber is "the write whose result I read".
          MemoryAccess *Clob = Walker.getClobberingMemoryAccess(MU);
          outs() << "  load in '" << BB.getName() << "' reads result of: ";
          // The clobber node may be a MemoryDef (some write), a MemoryUse
          // (rare), or an entry/lifetime node with no instruction behind it.
          const Instruction *Src = nullptr;
          if (auto *Def = dyn_cast<MemoryDef>(Clob))
            Src = Def->getMemoryInst();
          else if (auto *Use2 = dyn_cast<MemoryUse>(Clob))
            Src = Use2->getMemoryInst();
          if (Src)
            outs() << *Src << "\n";
          else
            outs() << "(live-on-entry / nothing written yet)\n";
        } else if (auto *MD = dyn_cast<MemoryDef>(MA)) {
          // A write: asking "is my version consumed by anything" is the
          // DSE question -- 16 picks that up.
          MemoryAccess *Clob = Walker.getClobberingMemoryAccess(MD);
          (void)Clob; // not expanding on it here, see 16
        }
      }

    // Read-only.
    return PreservedAnalyses::all();
  }
};

} // end anonymous namespace

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "MemorySSADemo", LLVM_VERSION_STRING,
          [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                [](StringRef Name, FunctionPassManager &FPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                  if (Name != "memoryssa-demo")
                    return false;
                  FPM.addPass(MemorySSADemoPass());
                  return true;
                });
          }};
}

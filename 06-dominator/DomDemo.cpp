//=============================================================================
// 06: the DominatorTree. A dominates B iff every path from entry to B
// goes through A; the immediate dominator is the parent in the tree.
// The reading of it that stuck with me: the idom chain is "what must
// have already executed by the time this block runs".
//
// The two queries the rest of this repo keeps using:
//   DT.dominates(A, B)             can I use A's value inside B?
//   DT.getNode(B)->getIDom()       walk up towards the entry
// Unreachable blocks have no idom, so check isReachableFromEntry before
// touching nodes.
//
// opt -load-pass-plugin .../DomDemo.so -passes=dom-demo -disable-output examples/loops.ll
//=============================================================================

#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Plugins/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace {

class DomDemoPass : public RequiredPassInfoMixin<DomDemoPass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM) {
    DominatorTree &DT = AM.getResult<DominatorTreeAnalysis>(F);

    outs() << "==================== domtree: " << F.getName()
           << " ====================\n";
    // Dump the raw CFG first, then the dominator tree, and compare shapes.
    for (BasicBlock &BB : F) {
      outs() << "  " << BB.getName() << " -> ";
      if (succ_size(&BB) == 0) {
        outs() << "(exit)\n";
        continue;
      }
      for (BasicBlock *S : successors(&BB))
        outs() << S->getName() << " ";
      outs() << "\n";
    }
    outs() << "--- dominator tree ---\n";
    DT.print(outs());

    // ---- dominance queries ---------------------------------------------
    // Ask "who dominates who" between arbitrary blocks. Sanity check: the
    // entry dominates everything.
    BasicBlock &Entry = F.getEntryBlock();
    for (BasicBlock &BB : F) {
      if (&BB == &Entry)
        continue;
      bool Dom = DT.dominates(&Entry, &BB);
      outs() << "  entry dominates " << BB.getName()
             << " ? " << (Dom ? "yes" : "NO (!)") << "\n";
    }

    // ---- walking up the idom chain: print each block's dominance chain -
    // This chain is exactly "the set of blocks that must have executed by
    // the time BB runs" -- and that's also the whole safety argument
    // behind LICM's preheader hoisting (07 does that).
    outs() << "--- dominance chains ---\n";
    for (BasicBlock &BB : F) {
      if (!DT.isReachableFromEntry(&BB)) {
        outs() << "  " << BB.getName() << " : (unreachable)\n";
        continue;
      }
      SmallVector<StringRef, 8> Chain;
      const DomTreeNode *N = DT.getNode(&BB);
      while (N) {
        Chain.push_back(N->getBlock()->getName());
        N = N->getIDom(); // getIDom(entry) == nullptr, chain ends there
      }
      outs() << "  " << BB.getName() << " : ";
      for (auto It = Chain.rbegin(); It != Chain.rend(); ++It)
        outs() << *It << (It + 1 != Chain.rend() ? " -> " : "\n");
    }

    return PreservedAnalyses::all();
  }
};

} // end anonymous namespace

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "DomDemo", LLVM_VERSION_STRING,
          [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                [](StringRef Name, FunctionPassManager &FPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                  if (Name != "dom-demo")
                    return false;
                  FPM.addPass(DomDemoPass());
                  return true;
                });
          }};
}

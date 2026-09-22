//=============================================================================
// 11: infer memory effects from a function body, attach the attribute,
// then delete the dead calls. This one went through the most revisions
// of anything here, because of one discovery: marking a function
// memory(none) is NOT enough for its dead calls to go away.
// mayHaveSideEffects is mayThrow || mayWrite || !willReturn, so the call
// survives until the function is also nounwind and willreturn. Found
// that by staring at debug output for an embarrassing amount of time.
//
// (Also, recent LLVM: Attribute::ReadNone is gone, the verifier rejects
// it. getWithMemoryEffects(MemoryEffects::none()) is the current
// spelling.)
//
// opt -load-pass-plugin .../PurityDemo.so -passes=purity-demo -S examples/purity.ll
//=============================================================================

#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Plugins/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace {

class PurityDemoPass : public RequiredPassInfoMixin<PurityDemoPass> {
public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &) {
    unsigned Marked = 0, Removed = 0;

    for (Function &F : M) {
      if (F.isDeclaration())
        continue;

      // Analyze: does this function touch memory / call other functions?
      // Body-only: any call to another function disqualifies it. (The real
      // function-attrs walks callgraph SCCs for the transitive closure --
      // this pass does not, see the wrinkle below.)
      //
      // Wrinkle the test caught: dead calls get erased right here, in the
      // same scan. By the time the loop reaches `caller`, its call to
      // `pure` is already gone, so `caller` comes up clean and gets marked
      // too. That propagation-by-erasure is order-dependent luck, not a
      // fixpoint -- reverse the definition order and the count changes.
      // Sweep until nothing changes and it stops being luck.
      bool TouchesMemory = false, CallsOthers = false;
      for (Instruction &I : instructions(F)) {
        if (isa<StoreInst>(&I) || isa<LoadInst>(&I)) {
          // Touching alloca locals is fine (stack temporaries nobody
          // outside can observe).
          const Value *Ptr = isa<StoreInst>(&I)
                                 ? cast<StoreInst>(&I)->getPointerOperand()
                                 : cast<LoadInst>(&I)->getPointerOperand();
          if (!isa<AllocaInst>(Ptr->stripPointerCasts()))
            TouchesMemory = true;
        } else if (auto *CB = dyn_cast<CallBase>(&I)) {
          if (CB->getCalledFunction() == &F)
            TouchesMemory = true; // recursion: be conservative, call it impure
          else
            CallsOthers = true; // simplified: any other call = give up
        } else if (isa<AtomicCmpXchgInst>(&I) || isa<AtomicRMWInst>(&I)) {
          TouchesMemory = true;
        }
      }

      if (TouchesMemory || CallsOthers)
        continue;

      // Annotate: readnone means "neither reads nor writes any memory the
      // caller can observe". Note LLVM moved readnone/readonly onto the
      // unified memory(none) effects attribute a while ago -- the old
      // Attribute::ReadNone fails the verifier with "does not apply to
      // functions" now.
      F.addFnAttr(Attribute::getWithMemoryEffects(
          F.getContext(), MemoryEffects::none()));
      // Also infer nounwind: no calls in the body, so it can't throw.
      // Without it, a dead call to a pure function still counts as
      // "may have side effects" (unwinding is a side effect too) and
      // can't be deleted.
      F.addFnAttr(Attribute::NoUnwind);
      // willreturn: no calls, no waiting loops -- it must return.
      // mayHaveSideEffects is really mayThrow || mayWrite || !willReturn,
      // so all three conditions have to be inferred before a call counts
      // as fully side-effect-free. I found this one the hard way, by
      // staring at debug output.
      F.addFnAttr(Attribute::WillReturn);
      outs() << "  [purity] '" << F.getName() << "' is memory(none)\n";
      ++Marked;

      // Closing the loop: a call to a readnone function whose result is
      // unused is dead code. In the production pipeline ADCE/DSE handle
      // this uniformly through the attributes.
      SmallVector<CallBase *, 4> Dead;
      for (Use &U : F.uses())
        if (auto *CB = dyn_cast<CallBase>(U.getUser()))
          if (CB->getCalledOperand() == &F && CB->use_empty() &&
              !CB->mayHaveSideEffects())
            Dead.push_back(CB);
      for (CallBase *CB : Dead) {
        outs() << "  [purity] removing dead call to '" << F.getName() << "'\n";
        CB->eraseFromParent();
        ++Removed;
      }
    }

    outs() << "purity-demo: marked " << Marked
           << " function(s) memory(none), removed " << Removed << " dead call(s)\n";
    // Added attributes, deleted instructions -- both invalidate downstream
    // analyses, so: none().
    return PreservedAnalyses::none();
  }
};

} // end anonymous namespace

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "PurityDemo", LLVM_VERSION_STRING,
          [](PassBuilder &PB) {
            // Note this is a ModulePass: attribute inference + dead call
            // removal is naturally module-level work (you need to see all
            // the functions).
            PB.registerPipelineParsingCallback(
                [](StringRef Name, ModulePassManager &MPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                  if (Name != "purity-demo")
                    return false;
                  MPM.addPass(PurityDemoPass());
                  return true;
                });
          }};
}

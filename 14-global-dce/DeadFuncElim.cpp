//=============================================================================
// 14: globaldce-lite. A function pass can never see dead functions -- it
// only gets one function at a time. So this one works at module level:
// color the call graph starting from everything the outside world can
// reach, and whatever internal function ends up uncolored is dead.
//
// The root-set detail that is easy to miss: address-taken functions.
// Storing a function pointer into a callback table is not a "call", but
// the runtime can still reach it through that pointer later, so any
// non-call use makes a function a root. Declarations without bodies
// (printf etc.) are just skipped in the walk.
//
// opt -load-pass-plugin .../DeadFuncElim.so -passes=dead-func-elim -S examples/deadfunc.ll
//=============================================================================

#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Plugins/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"

#include <vector>

using namespace llvm;

namespace {

static bool hasAddressTaken(Function &F) {
  // Walk every use of the function: being the called operand of a CallBase
  // is a "real call". Any other use (stored into a function-pointer array,
  // gep'd, passed as an argument) means the address escaped.
  for (Use &U : F.uses()) {
    if (auto *CB = dyn_cast<CallBase>(U.getUser())) {
      if (!CB->isCallee(&U))
        return true;
    } else {
      return true; // a non-call user: the address leaked out
    }
  }
  return false;
}

class DeadFuncElimPass : public RequiredPassInfoMixin<DeadFuncElimPass> {
public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &) {
    // Roots: everything the outside world can get its hands on.
    SmallPtrSet<Function *, 16> Live;
    std::vector<Function *> Worklist;
    for (Function &F : M) {
      if (F.isDeclaration())
        continue;
      if (!F.hasLocalLinkage() || hasAddressTaken(F)) {
        Live.insert(&F);
        Worklist.push_back(&F);
      }
    }

    // Color from the roots along call edges. Only direct calls with bodies
    // count -- indirect calls through function pointers can't be resolved
    // statically, and any function containing them is already protected by
    // the address-taken check above.
    while (!Worklist.empty()) {
      Function *F = Worklist.back();
      Worklist.pop_back();
      for (Instruction &I : instructions(*F))
        if (auto *CB = dyn_cast<CallBase>(&I))
          if (auto *Callee = CB->getCalledFunction())
            if (!Callee->isDeclaration() && Live.insert(Callee).second)
              Worklist.push_back(Callee);
    }

    // Untouched internal functions: remove. RAUW-ing with nothing is
    // pointless -- nothing uses them (any use would have made them live),
    // so erase directly.
    unsigned Removed = 0;
    for (auto It = M.begin(); It != M.end();) {
      Function &F = *It;
      ++It;
      if (F.hasLocalLinkage() && !F.isDeclaration() && !Live.count(&F)) {
        outs() << "  [dead-func] removing '" << F.getName() << "'\n";
        F.eraseFromParent();
        ++Removed;
      }
    }

    outs() << "dead-func-elim: removed " << Removed << " function(s)\n";
    return Removed ? PreservedAnalyses::none() : PreservedAnalyses::all();
  }
};

} // namespace

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "DeadFuncElim", LLVM_VERSION_STRING,
          [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                [](StringRef Name, ModulePassManager &MPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                  if (Name != "dead-func-elim")
                    return false;
                  MPM.addPass(DeadFuncElimPass());
                  return true;
                });
          }};
}

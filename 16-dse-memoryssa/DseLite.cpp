//=============================================================================
// 16: DSE on MemorySSA, the real version of 03's two-adjacent-stores
// toy. A MemoryDef is dead when the answer to both is yes:
//   - does the next clobbering access fully overwrite me? (MustAlias,
//     same size)
//   - did anything read my version before that? In MemorySSA terms this
//     is just "no MemoryUse consumes my def".
//
// Intrablock only: the scan stops at the end of the block. The real pass
// keeps going through MemoryPhis and across blocks with the walker plus
// dominance info. Same idea, a lot more bookkeeping. Also no escape
// analysis for calls.
//
// One that bit me: eraseFromParent on the store before removing its
// MemoryAccess leaves MemorySSA holding a dangling access.
// MemorySSAUpdater::removeMemoryAccess first, then erase the
// instruction.
//
// Another one, caught by a test after this file was "done": MustAlias
// only pins the address, not the width -- a narrow store does not kill a
// wide one, so isKilled has to compare sizes too.
//=============================================================================

#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/MemorySSA.h"
#include "llvm/Analysis/MemorySSAUpdater.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Plugins/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace {

// Scan forward from MD through the block's access list. A store is dead if
// we hit a same-location store before any load that could read it.
// Unrelated accesses (NoAlias) don't stop the scan, they just get skipped.
static bool isKilled(MemorySSA::AccessList::iterator It,
                     MemorySSA::AccessList::iterator End, StoreInst *SI,
                     AAResults &AA) {
  MemoryLocation L = MemoryLocation::get(SI);
  MemorySSA::AccessList::iterator Next = It;
  for (++Next; Next != End; ++Next) {
    auto &Access = *Next;
    if (auto *MU = dyn_cast<MemoryUse>(&Access)) {
      auto *LI = dyn_cast<LoadInst>(MU->getMemoryInst());
      if (!LI)
        return false; // some other kind of read, play it safe
      if (AA.alias(L, MemoryLocation::get(LI)) != AliasResult::NoAlias)
        return false; // somebody reads us first -> the store is live
      // unrelated load, keep looking
      continue;
    }
    if (auto *MD2 = dyn_cast<MemoryDef>(&Access)) {
      auto *SI2 = dyn_cast<StoreInst>(MD2->getMemoryInst());
      if (!SI2 || SI2->isVolatile())
        return false; // a call or an atomic: may touch anything, stop
      AliasResult AR = AA.alias(L, MemoryLocation::get(SI2));
      if (AR == AliasResult::NoAlias)
        continue; // unrelated store in between, keep scanning
      // MustAlias pins the address, not the size: store i32 at the same
      // address is a MustAlias of store i64 but only overwrites the low
      // half, so the killer has to be at least as wide too.
      if (AR == AliasResult::MustAlias &&
          MemoryLocation::get(SI2).Size.getValue() >= L.Size.getValue())
        return true; // same address, fully covered -> dead
      return false;  // partial overlap or a narrower killer -> play it safe
    }
    return false; // a MemoryPhi shouldn't appear mid-block; be safe anyway
  }
  return false; // reached the end of the block without a killer
}

class DseLitePass : public RequiredPassInfoMixin<DseLitePass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM) {
    MemorySSA &MSSA = AM.getResult<MemorySSAAnalysis>(F).getMSSA();
    AAResults &AA = AM.getResult<AAManager>(F);

    // Read-only phase: collect dead stores while the MSSA is untouched.
    SmallVector<StoreInst *, 16> Dead;
    for (BasicBlock &BB : F) {
      MemorySSA::AccessList *AL = MSSA.getBlockAccesses(&BB);
      if (!AL)
        continue;
      for (auto It = AL->begin(); It != AL->end(); ++It) {
        auto *MD = dyn_cast<MemoryDef>(&*It);
        if (!MD)
          continue;
        auto *SI = dyn_cast<StoreInst>(MD->getMemoryInst());
        if (!SI || SI->isVolatile())
          continue;
        if (isKilled(It, AL->end(), SI, AA)) {
          outs() << "  [dse] removing dead store: " << *SI << "\n";
          Dead.push_back(SI);
        }
      }
    }

    // Mutation phase. Don't just eraseFromParent(): every store owns a
    // MemoryDef in the MSSA, so the access has to leave the chain first or
    // the MSSA is left holding a dangling pointer. MemorySSAUpdater also
    // patches up the def-use edges around the hole we're making.
    if (!Dead.empty()) {
      MemorySSAUpdater MSSAU(&MSSA);
      for (StoreInst *SI : Dead) {
        MSSAU.removeMemoryAccess(SI);
        SI->eraseFromParent();
      }
    }

    outs() << "dse-lite: removed " << Dead.size() << " dead store(s) in '"
           << F.getName() << "'\n";
    return Dead.empty() ? PreservedAnalyses::all() : PreservedAnalyses::none();
  }
};

} // end anonymous namespace

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "DseLite", LLVM_VERSION_STRING,
          [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                [](StringRef Name, FunctionPassManager &FPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                  if (Name != "dse-lite")
                    return false;
                  FPM.addPass(DseLitePass());
                  return true;
                });
          }};
}

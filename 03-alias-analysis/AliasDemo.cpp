//=============================================================================
// 03: alias analysis from the caller's side. Build a MemoryLocation
// (pointer, size, aatags), ask AAResults, get Must/Partial/May/No back.
// AAResults chains every registered AA (BasicAA, ScopedNoAliasAA,
// TypeBasedAA, ...).
//
// The reframe that made it click for me: AA does not answer "do these
// two pointers overlap". It answers "could a write to one change what a
// read of the other returns".
//
// The toy DSE at the bottom deletes a store that the very next store
// overwrites. Adjacent stores only -- that's all a pairwise check can
// see, which is exactly the gap 16 closes with MemorySSA.
//=============================================================================

#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Plugins/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace {

static const char *toString(AliasResult::Kind K) {
  switch (K) {
  case AliasResult::NoAlias:     return "NoAlias";
  case AliasResult::MayAlias:    return "MayAlias";
  case AliasResult::PartialAlias:return "PartialAlias";
  case AliasResult::MustAlias:   return "MustAlias";
  }
  return "?";
}

class AliasDemoPass : public RequiredPassInfoMixin<AliasDemoPass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM) {
    // Get the AA aggregate for this function. AAResults chains every
    // registered analysis; a query walks the chain and any NoAlias along
    // the way is authoritative (the contract is "NoAlias is reliable,
    // anything else means we couldn't prove it" -> MayAlias is the
    // conservative fallback).
    AAResults &AA = AM.getResult<AAManager>(F);

    outs() << "==================== alias: " << F.getName() << " ====================\n";

    // Collect all stores in the function and query every pair.
    SmallVector<StoreInst *, 16> Stores;
    for (Instruction &I : instructions(F))
      if (auto *S = dyn_cast<StoreInst>(&I))
        Stores.push_back(S);

    unsigned NumNoAlias = 0, NumMustAlias = 0;
    for (unsigned i = 0; i < Stores.size(); ++i)
      for (unsigned j = i + 1; j < Stores.size(); ++j) {
        MemoryLocation L1 = MemoryLocation::get(Stores[i]);
        MemoryLocation L2 = MemoryLocation::get(Stores[j]);
        AliasResult AR = AA.alias(L1, L2);
        outs() << "  store#" << i << " vs store#" << j << ": "
               << toString(ARKind(AR)) << "\n";
        NumNoAlias += AR == AliasResult::NoAlias;
        NumMustAlias += AR == AliasResult::MustAlias;
      }

    // ---- the toy DSE: dead store removal ------------------------------
    // store#i is dead if every later access to the same location
    // overwrites it and nothing reads in between. Here I only do the most
    // local, most conservative version: two *adjacent* stores where the
    // second MustAliases the same location and is at least as wide ->
    // the first one dies. The width check matters: MustAlias pins the
    // address, not the size, so store i32 does not kill store i64. The
    // real -dse uses MemorySSA to turn "is there any access between these
    // two" into an O(1) query -- MemorySSA exists precisely for this, see
    // 04 and 16, where I actually use it.
    unsigned Deleted = 0;
    for (unsigned i = 0; i + 1 < Stores.size(); ++i) {
      if (!Stores[i] || !Stores[i + 1] || Stores[i]->isVolatile() ||
          Stores[i + 1]->isVolatile())
        continue;
      MemoryLocation L1 = MemoryLocation::get(Stores[i]);
      MemoryLocation L2 = MemoryLocation::get(Stores[i + 1]);
      if (Stores[i]->getNextNode() != Stores[i + 1])
        continue; // adjacent only, so nothing can be in between
      AliasResult AR = AA.alias(L1, L2);
      if (AR == AliasResult::MustAlias &&
          L2.Size.getValue() >= L1.Size.getValue()) {
        Stores[i]->eraseFromParent();
        Stores[i] = nullptr;
        ++Deleted;
        outs() << "  [dse-demo] removed dead store #" << i << "\n";
      }
    }

    outs() << "  pairs=" << Stores.size() * (Stores.size() - 1) / 2
           << ", NoAlias=" << NumNoAlias
           << ", MustAlias=" << NumMustAlias
           << ", dead stores removed=" << Deleted << "\n";

    // We deleted instructions -> every IR-dependent analysis (AA included)
    // is stale. Say so and let the PM decide what to rebuild.
    return PreservedAnalyses::allInSet<CFGAnalyses>();
  }

private:
  static AliasResult::Kind ARKind(const AliasResult &AR) {
    return (AliasResult::Kind)AR; // AliasResult converts explicitly to Kind
  }
};

} // end anonymous namespace

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "AliasDemoPass", LLVM_VERSION_STRING,
          [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                [](StringRef Name, FunctionPassManager &FPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                  if (Name != "alias-demo")
                    return false;
                  FPM.addPass(AliasDemoPass());
                  return true;
                });
          }};
}

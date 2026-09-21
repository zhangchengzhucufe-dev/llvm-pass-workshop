//=============================================================================
// 10: GEP and DataLayout. The thing I had wrong until I wrote this: GEP
// never touches memory. It is pure pointer arithmetic; the result is
// just a new address value.
//
// The type ladder decides the step sizes: index 0 steps by the source
// type's size, each following index steps by the type it drills into.
// DataLayout supplies the numbers -- getTypeAllocSize for sizeof,
// getElementOffset for offsetof -- and alignment padding between fields
// is where C's "field order changes the struct size" comes from.
//
// opt -load-pass-plugin .../GepDemo.so -passes=gep-demo -disable-output examples/struct.ll
//=============================================================================

#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Plugins/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace {

class GepDemoPass : public RequiredPassInfoMixin<GepDemoPass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM) {
    const DataLayout &DL = F.getDataLayout(); // every module carries the
                                              // target's memory model

    outs() << "==================== gep-demo: " << F.getName()
           << " ====================\n";
    outs() << "  pointer size = " << DL.getPointerSize() * 8 << " bits"
           << ", i64 align = " << DL.getABITypeAlign(Type::getInt64Ty(F.getContext())).value()
           << " bytes\n";

    for (BasicBlock &BB : F)
      for (Instruction &I : BB) {
        auto *GEP = dyn_cast<GetElementPtrInst>(&I);
        if (!GEP)
          continue;

        outs() << "\n  " << I << "\n";

        // ---- take the type ladder apart -------------------------------
        // GEP semantics are decided by "source type + index sequence":
        // gep_type_iterator drills down through struct/array levels along
        // the indices, and each level's step size is sizeof(that level's
        // type). Index 0 always steps by sizeof(source type); later
        // indices drill deeper.
        outs() << "    step sizes: ";
        unsigned Level = 0;
        for (auto GTI = gep_type_begin(GEP), GTE = gep_type_end(GEP);
             GTI != GTE; ++GTI, ++Level)
          outs() << "[idx" << Level << "] sizeof(" << *GTI.getIndexedType()
                 << ") = " << DL.getTypeAllocSize(GTI.getIndexedType())
                 << "B; ";
        outs() << "\n";

        // ---- struct field offsets -------------------------------------
        // For a struct GEP with constant indices, the byte offset is fully
        // computable at compile time.
        if (auto *STy = dyn_cast<StructType>(GEP->getSourceElementType())) {
          outs() << "    struct layout of " << *STy << ":\n";
          for (unsigned Elem = 0; Elem < STy->getNumElements(); ++Elem) {
            Type *FTy = STy->getElementType(Elem);
            // getElementOffset is C's offsetof; the gap between where you'd
            // naively expect a field and its real offset is the padding.
            outs() << "      field " << Elem << " (" << *FTy << "): offset="
                   << DL.getStructLayout(STy)->getElementOffset(Elem)
                   << ", size=" << DL.getTypeAllocSize(FTy) << "\n";
          }
          outs() << "      total size = "
                 << DL.getStructLayout(STy)->getSizeInBytes()
                 << " (including tail padding)\n";
        }

        // ---- constant-index GEP -> resolve the byte offset outright ---
        // GEP doesn't read memory, but the offset can often be determined
        // entirely at compile time.
        APInt Offset(64, 0);
        if (GEP->accumulateConstantOffset(DL, Offset))
          outs() << "    constant byte offset from base: " << Offset << "\n";
        else
          outs() << "    offset depends on runtime indices\n";
      }

    return PreservedAnalyses::all();
  }
};

} // end anonymous namespace

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "GepDemo", LLVM_VERSION_STRING,
          [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                [](StringRef Name, FunctionPassManager &FPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                  if (Name != "gep-demo")
                    return false;
                  FPM.addPass(GepDemoPass());
                  return true;
                });
          }};
}

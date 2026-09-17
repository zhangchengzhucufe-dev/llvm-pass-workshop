//=============================================================================
// 09: peephole simplification, i64 add/sub/mul only. The whole loop is:
// match one instruction, replaceAllUsesWith, erase. That is instcombine's
// daily routine.
//
//   const,const                    fold it right here
//   x+0, 0+x, x*1, 1*x, sub x,0    -> x
//   sub x,x                        -> 0
//   x*0, 0*x                       -> 0
//
// The outer loop runs to a fixpoint because the rules unlock each other:
// once x+0 collapses, (x+0)*1 becomes visible to the next sweep.
//
// Order matters when replacing: replaceAllUsesWith first, THEN
// eraseFromParent (which also unlinks the instruction from its block).
//=============================================================================

#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Plugins/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace {

// Try to simplify one instruction. Returns the replacement value on a
// match, nullptr otherwise.
// Note the replacement may be a constant or an operand of some other
// instruction -- replaceAllUsesWith treats both the same. That's SSA
// being nice: renaming a name propagates to every use automatically,
// no dangling references to worry about.
static Value *simplifyOne(Instruction &I) {
  auto *BO = dyn_cast<BinaryOperator>(&I);
  if (!BO)
    return nullptr;

  Value *LHS = BO->getOperand(0), *RHS = BO->getOperand(1);
  auto *LC = dyn_cast<ConstantInt>(LHS), *RC = dyn_cast<ConstantInt>(RHS);

  // ---- rule 1: constant folding ---------------------------------------
  // Compute 3 + 4 at compile time. ConstantInt::get turns an IR
  // instruction into a compile-time computation.
  if (LC && RC) {
    if (BO->getOpcode() == Instruction::Add)
      return ConstantInt::get(BO->getType(),
                              LC->getValue() + RC->getValue());
    if (BO->getOpcode() == Instruction::Sub)
      return ConstantInt::get(BO->getType(),
                              LC->getValue() - RC->getValue());
    if (BO->getOpcode() == Instruction::Mul)
      return ConstantInt::get(BO->getType(),
                              LC->getValue() * RC->getValue());
  }

  // ---- rule 2: algebraic identities ------------------------------------
  switch (BO->getOpcode()) {
  case Instruction::Add:
    if (RC && RC->isZero())
      return LHS; // x + 0
    if (LC && LC->isZero())
      return RHS; // 0 + x
    break;
  case Instruction::Sub:
    if (RC && RC->isZero())
      return LHS; // x - 0
    if (LHS == RHS)
      return ConstantInt::get(BO->getType(), 0); // x - x
    break;
  case Instruction::Mul:
    if (RC && RC->isOne())
      return LHS; // x * 1
    if (LC && LC->isOne())
      return RHS; // 1 * x
    if ((RC && RC->isZero()) || (LC && LC->isZero()))
      return ConstantInt::get(BO->getType(), 0); // x * 0
    break;
  default:
    break;
  }
  return nullptr;
}

class InstSimplifyDemoPass : public RequiredPassInfoMixin<InstSimplifyDemoPass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &) {
    unsigned Total = 0;

    // The outer loop is the fixpoint iteration. One simplification
    // shortens the instruction chain and exposes new opportunities. Real
    // instcombine uses a fixed budget + worklist; the teaching version
    // just repeats "until nothing changed".
    bool Changed = true;
    while (Changed) {
      Changed = false;
      // Watch out for iterator invalidation: eraseFromParent removes the
      // current node, so grab next before touching anything.
      for (BasicBlock &BB : F)
        for (auto It = BB.begin(); It != BB.end();) {
          Instruction &I = *It;
          ++It;
          if (Value *Repl = simplifyOne(I)) {
            outs() << "  [simplify] " << I << "  ->  " << *Repl->getType()
                   << " " << *Repl << "\n";
            I.replaceAllUsesWith(Repl);
            I.eraseFromParent();
            ++Total;
            Changed = true;
          }
        }
    }

    outs() << "instsimplify-demo: simplified " << Total
           << " instruction(s) in '" << F.getName() << "'\n";
    return Total ? PreservedAnalyses::none() : PreservedAnalyses::all();
  }
};

} // end anonymous namespace

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "InstSimplifyDemo", LLVM_VERSION_STRING,
          [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                [](StringRef Name, FunctionPassManager &FPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                  if (Name != "instsimplify-demo")
                    return false;
                  FPM.addPass(InstSimplifyDemoPass());
                  return true;
                });
          }};
}

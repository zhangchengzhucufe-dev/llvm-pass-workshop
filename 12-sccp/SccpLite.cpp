//=============================================================================
// 12: SCCP lite. Every value sits on a three-level lattice,
//
//   UNKNOWN < CONST < OVERDEFINED
//
// which only moves up, never down, so iteration has to converge. Two
// things separate this from the block-level dataflow in 02: state lives
// on SSA values instead of block edges (that's the "sparse"), and a
// branch on a constant only walks the live edge, so the dead side never
// gets marked executable at all.
//
// My first version folded nothing: the lattice was keyed by instruction,
// and constant literals are not instructions, so every constant read as
// UNKNOWN forever. The lookup now special-cases ConstantInt; the
// comment sits at the fix site.
//
// Not in here vs the real SCCP.cpp: undef handling, address-taken
// tracking, deleting unreachable blocks. Phis converge only when all
// executable predecessors agree.
//
// opt -load-pass-plugin .../SccpLite.so -passes=sccp-lite -S examples/sccp.ll
//=============================================================================

#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Plugins/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"

#include <vector>

using namespace llvm;

namespace {

struct Lattice {
  enum Kind { Unknown, Const, Over };
  Kind K = Unknown;
  ConstantInt *C = nullptr;

  static Lattice constant(ConstantInt *V) { return {Const, V}; }
  static Lattice over() { return {Over, nullptr}; }

  // The lattice meet: what happens when two propagation sources collide.
  // unknown merged with anything is that thing (it's the lattice bottom);
  // two *different* constants meeting means the truth is "decided at
  // runtime" (over).
  static Lattice merge(const Lattice &A, const Lattice &B) {
    if (A.K == Unknown)
      return B;
    if (B.K == Unknown)
      return A;
    if (A.K == Const && B.K == Const && A.C == B.C)
      return A;
    return over();
  }
};

class SccpSolver {
public:
  explicit SccpSolver(Function &F) : F(F) { solve(); }

  const DenseMap<Value *, Lattice> &values() const { return ValueStates; }
  bool isExec(BasicBlock *BB) const { return ExecBBs.count(BB); }

private:
  Function &F;
  DenseMap<Value *, Lattice> ValueStates;
  SmallPtrSet<BasicBlock *, 16> ExecBBs;
  std::vector<BasicBlock *> BBWorklist;
  std::vector<Instruction *> InstWorklist;

  void markExec(BasicBlock *BB) {
    if (ExecBBs.insert(BB).second)
      BBWorklist.push_back(BB);
  }

  // When a value's state changes, re-push the instructions that use it.
  // This is the whole trick behind "sparse": only instructions actually
  // touched by the change get recomputed, instead of rescanning the whole
  // function every round like the liveness pass (02) does.
  void changed(Value *V) {
    for (User *U : V->users())
      if (auto *I = dyn_cast<Instruction>(U))
        if (ExecBBs.count(I->getParent()))
          InstWorklist.push_back(I);
  }

  void visitInst(Instruction &I) {
    // phi: merge the values coming in from all *executable* predecessors.
    // If a predecessor isn't executable, its incoming value never
    // actually happens and must not be counted -- that's where SCCP gets
    // its extra precision.
    if (auto *PN = dyn_cast<PHINode>(&I)) {
      Lattice Acc;
      for (unsigned i = 0; i < PN->getNumIncomingValues(); ++i)
        if (ExecBBs.count(PN->getIncomingBlock(i)))
          Acc = Lattice::merge(Acc, stateOf(PN->getIncomingValue(i)));
      update(I, Acc);
      return;
    }

    // Binary op: fold when both operands are known, give up on any over,
    // otherwise wait.
    if (auto *BO = dyn_cast<BinaryOperator>(&I)) {
      Lattice L = stateOf(BO->getOperand(0));
      Lattice R = stateOf(BO->getOperand(1));
      if (L.K == Lattice::Over || R.K == Lattice::Over) {
        update(I, Lattice::over());
        return;
      }
      if (L.K == Lattice::Const && R.K == Lattice::Const) {
        Constant *Folded = ConstantFoldBinaryOpOperands(
            BO->getOpcode(), L.C, R.C, F.getParent()->getDataLayout());
        if (auto *CI = dyn_cast_or_null<ConstantInt>(Folded)) {
          update(I, Lattice::constant(CI));
          return;
        }
      }
      return; // still unknown, waiting on operands
    }

    // Branches: a constant condition only walks one edge; the other side
    // never gets marked executable.
    // (This LLVM cut BranchInst apart into UncondBrInst / CondBrInst.)
    if (auto *UBI = dyn_cast<UncondBrInst>(&I)) {
      markExec(UBI->getSuccessor(0));
      return;
    }
    if (auto *CBI = dyn_cast<CondBrInst>(&I)) {
      Lattice Cond = stateOf(CBI->getCondition());
      if (Cond.K == Lattice::Const) {
        markExec(CBI->getSuccessor(Cond.C->isZero() ? 1 : 0));
      } else {
        markExec(CBI->getSuccessor(0));
        markExec(CBI->getSuccessor(1));
      }
      return;
    }

    update(I, Lattice::over());
  }

  // Current lattice state of any value. A constant *literal* is not an
  // instruction, so it has no entry in the state table -- it's Const by
  // nature. I missed this the first time around and 5+6 refused to fold
  // (everything stuck in unknown). The fix lives right here.
  Lattice stateOf(Value *V) const {
    if (auto *CI = dyn_cast<ConstantInt>(V))
      return Lattice::constant(CI);
    return ValueStates.lookup(V);
  }

  void update(Instruction &I, const Lattice &New) {
    Lattice &Old = ValueStates[&I];
    // Monotonicity check: the lattice only goes up. If this ever trips,
    // the solver has a bug.
    assert((Old.K == Lattice::Unknown || New.K == Lattice::Over ||
            (Old.K == Lattice::Const && New.K == Lattice::Const &&
             Old.C == New.C)) &&
           "lattice went down?");
    if (Old.K == New.K && Old.C == New.C)
      return;
    Old = New;
    changed(&I);
  }

  void solve() {
    markExec(&F.getEntryBlock());
    // Arguments are runtime values as far as the caller is concerned, so
    // start them at over (interprocedural propagation is a whole other
    // project).
    for (Argument &A : F.args()) {
      ValueStates[&A] = Lattice::over();
      changed(&A);
    }

    while (!BBWorklist.empty() || !InstWorklist.empty()) {
      while (!InstWorklist.empty()) {
        Instruction *I = InstWorklist.back();
        InstWorklist.pop_back();
        if (ExecBBs.count(I->getParent()))
          visitInst(*I);
      }
      if (!BBWorklist.empty()) {
        BasicBlock *BB = BBWorklist.back();
        BBWorklist.pop_back();
        for (Instruction &I : *BB)
          visitInst(I);
      }
    }
  }
};

class SccpLitePass : public RequiredPassInfoMixin<SccpLitePass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &) {
    SccpSolver Solver(F);
    unsigned Folded = 0;

    for (BasicBlock &BB : F) {
      if (!Solver.isExec(&BB))
        continue;
      for (auto It = BB.begin(); It != BB.end();) {
        Instruction &I = *It;
        ++It;
        const Lattice &LV = Solver.values().lookup(&I);
        // Instructions that resolved to a constant: RAUW with the
        // constant and delete. Don't touch terminators or phis though --
        // constant phis need copy propagation (the real SCCP does it,
        // I punted here).
        if (LV.K == Lattice::Const && !isa<PHINode>(I) && !I.isTerminator()) {
          I.replaceAllUsesWith(LV.C);
          I.eraseFromParent();
          ++Folded;
        }
      }
    }

    outs() << "sccp-lite: folded " << Folded << " instruction(s) in '"
           << F.getName() << "'\n";
    return Folded ? PreservedAnalyses::none() : PreservedAnalyses::all();
  }
};

} // namespace

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "SccpLite", LLVM_VERSION_STRING,
          [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                [](StringRef Name, FunctionPassManager &FPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                  if (Name != "sccp-lite")
                    return false;
                  FPM.addPass(SccpLitePass());
                  return true;
                });
          }};
}

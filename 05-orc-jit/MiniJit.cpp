//=============================================================================
// 05: a calculator REPL that goes all the way to machine code. The
// parser emits IR straight into a module, LLJIT compiles it, the REPL
// calls it. "define f(x) = ..." works too: redefinitions pick the newest
// version and recursive definitions work.
//
//   1 + 2 * 3              evaluate and print
//   define f(x) = x*x + 2*x + 1
//   f(10)                  -> 121, real JIT'd machine code
//   quit
//
// Three landmines, all of them stepped on (longer writeup in the README
// gotcha list): addModule takes the Module and doesn't give it back, and
// the LLVMContext has to move into the ThreadSafeModule with it; forget
// InitializeNativeTarget() and nothing runs; and the second REPL
// evaluation died with "duplicate definition" until every function got a
// unique name -- weak symbols do not save you.
//=============================================================================

#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/TargetSelect.h"

#include <cctype>
#include <cstdio>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace llvm;
using namespace llvm::orc;

//-----------------------------------------------------------------------------
// Part 1: a minimal recursive-descent parser.
// Grammar:
//   expr    := term (('+'|'-') term)*
//   term    := primary (('*'|'/') primary)*
//   primary := NUMBER | IDENT | '(' expr ')'
// A lexer cuts the input into a token stream first. "Lexer +
// recursive descent + emit IR directly" is exactly the Kaleidoscope
// tutorial skeleton, squeezed under 100 lines.
//-----------------------------------------------------------------------------

struct Token {
  enum Kind { Num, Ident, Op, LP, RP, End } K;
  std::string Text;
};

static std::vector<Token> lex(const std::string &S) {
  std::vector<Token> T;
  size_t i = 0;
  while (i < S.size()) {
    if (isspace(S[i])) { ++i; continue; }
    if (isdigit(S[i])) {
      size_t j = i;
      while (j < S.size() && (isdigit(S[j]) || S[j] == '.'))
        ++j;
      T.push_back({Token::Num, S.substr(i, j - i)});
      i = j;
    } else if (isalpha(S[i])) {
      size_t j = i;
      while (j < S.size() && isalnum(S[j]))
        ++j;
      T.push_back({Token::Ident, S.substr(i, j - i)});
      i = j;
    } else if (S[i] == '(') { T.push_back({Token::LP, "("}); ++i; }
    else if (S[i] == ')') { T.push_back({Token::RP, ")"}); ++i; }
    else { T.push_back({Token::Op, std::string(1, S[i])}); ++i; }
  }
  T.push_back({Token::End, ""});
  return T;
}

//-----------------------------------------------------------------------------
// Part 2: parse and emit IR at the same time.
// IR is emitted while parsing (syntax-directed translation). Every
// expression produces a chunk of code whose result is a Value* -- the
// concrete version of "SSA as an intermediate representation":
// every subexpression result is naturally one SSA name.
//-----------------------------------------------------------------------------

// The REPL's function table: user-visible name -> (internal name, arity).
// Redefinitions get a fresh internal name so the JITDylib never sees a
// duplicate -- the exact same trick the anonymous expressions use.
static std::map<std::string, std::pair<std::string, unsigned>> DefinedFuncs;
static unsigned DefCounter = 0;

class CodeGen {
public:
  CodeGen(LLVMContext &Ctx, Module &M, StringRef Name, ArrayRef<StringRef> Args)
      : Builder(Ctx), M(&M) {
    auto *Int64Ty = Type::getInt64Ty(Ctx);
    std::vector<Type *> ParamTys(Args.size(), Int64Ty);
    auto *FTy = FunctionType::get(Int64Ty, ParamTys, false);
    F = Function::Create(FTy, Function::ExternalLinkage, Name, M);
    // Name the formal parameters -- every later reference to x resolves
    // to this SSA value.
    unsigned Idx = 0;
    for (auto &A : F->args())
      A.setName(Args[Idx++]);
    BasicBlock *BB = BasicBlock::Create(Ctx, "entry", F);
    Builder.SetInsertPoint(BB);
  }

  // Everything is i64: a teaching demo doesn't do overflow rules or type
  // promotion.
  Value *emitExpr(std::vector<Token>::iterator &It,
                  std::vector<Token>::iterator End) {
    Value *LHS = emitTerm(It, End);
    while (It != End && It->K == Token::Op &&
           (It->Text == "+" || It->Text == "-")) {
      char Op = It->Text[0];
      ++It;
      Value *RHS = emitTerm(It, End);
      // A null RHS means the input ran out or was malformed ("1 +"): bail
      // out and let the caller print its compile-failed message. Passing
      // null into CreateAdd takes the whole REPL down.
      if (!LHS || !RHS)
        return nullptr;
      LHS = Op == '+' ? Builder.CreateAdd(LHS, RHS, "addtmp")
                      : Builder.CreateSub(LHS, RHS, "subtmp");
    }
    return LHS;
  }

private:
  IRBuilder<> Builder;
  Function *F;
  Module *M;

  Value *emitTerm(std::vector<Token>::iterator &It,
                  std::vector<Token>::iterator End) {
    Value *LHS = emitPrimary(It, End);
    while (It != End && It->K == Token::Op &&
           (It->Text == "*" || It->Text == "/")) {
      char Op = It->Text[0];
      ++It;
      Value *RHS = emitPrimary(It, End);
      if (!LHS || !RHS)
        return nullptr; // same bail-out as emitExpr, e.g. "1 *"
      LHS = Op == '*' ? Builder.CreateMul(LHS, RHS, "multmp")
                      : Builder.CreateSDiv(LHS, RHS, "divtmp");
    }
    return LHS;
  }

  Value *emitPrimary(std::vector<Token>::iterator &It,
                     std::vector<Token>::iterator End) {
    if (It == End)
      return nullptr;
    if (It->K == Token::Num) {
      int64_t V = strtoll(It->Text.c_str(), nullptr, 10);
      ++It;
      return ConstantInt::get(Type::getInt64Ty(Builder.getContext()), V);
    }
    if (It->K == Token::Ident) {
      StringRef Name = It->Text;
      ++It;
      // name followed by '(' is a call: declare the callee in this module
      // (ORC resolves it against the definition added by an earlier REPL
      // line) and emit a direct call.
      if (It != End && It->K == Token::LP) {
        ++It;
        auto Def = DefinedFuncs.find(std::string(Name));
        if (Def == DefinedFuncs.end()) {
          errs() << "error: unknown function '" << Name << "'\n";
          return nullptr;
        }
        std::vector<Value *> Args;
        while (true) {
          Value *A = emitExpr(It, End);
          if (!A)
            return nullptr;
          Args.push_back(A);
          if (It != End && It->K == Token::Op && It->Text == ",") {
            ++It;
            continue;
          }
          break;
        }
        if (It != End && It->K == Token::RP)
          ++It;
        if (Args.size() != Def->second.second) {
          errs() << "error: '" << Name << "' wants "
                 << Def->second.second << " argument(s), got " << Args.size()
                 << "\n";
          return nullptr;
        }
        auto *Int64Ty = Type::getInt64Ty(Builder.getContext());
        std::vector<Type *> ParamTys(Args.size(), Int64Ty);
        FunctionCallee Callee = M->getOrInsertFunction(
            Def->second.first, FunctionType::get(Int64Ty, ParamTys, false));
        return Builder.CreateCall(Callee, Args, "calltmp");
      }
      // plain variable reference -> the function argument's SSA value. A
      // real language frontend would have a proper symbol table here.
      for (auto &A : F->args())
        if (A.getName() == Name)
          return &A;
      errs() << "error: unknown identifier '" << Name << "'\n";
      return nullptr;
    }
    if (It->K == Token::LP) {
      ++It;
      Value *V = emitExpr(It, End);
      if (!V)
        return nullptr;
      if (It != End && It->K == Token::RP)
        ++It;
      return V;
    }
    errs() << "error: unexpected token '" << It->Text << "'\n";
    return nullptr;
  }
};

//-----------------------------------------------------------------------------
// Part 3: the JIT session
//-----------------------------------------------------------------------------

static ExitOnError ExitOnErr;

// Compile one "top-level expression": wrap it in an anonymous function
// __anon_expr and call it right after JITting.
static unsigned ExprCounter = 0;

static int64_t evalTopExpression(LLJIT &Jit, const std::string &Src) {
  // The context and module travel together: ThreadSafeModule takes shared
  // ownership of the context the module was built on, so the IR stays
  // valid no matter when ORC gets around to compiling it.
  auto Ctx = std::make_unique<LLVMContext>();
  auto M = std::make_unique<Module>("expr", *Ctx);

  // Every expression gets a unique function name: once a symbol exists in
  // the JITDylib (even a weak one), lookup can still hit the old version.
  // Unique names are the only reliable REPL pattern I found.
  std::string FnName = "__anon_expr." + std::to_string(ExprCounter++);

  std::vector<Token> Toks = lex(Src);
  auto It = Toks.begin();
  CodeGen CG(*Ctx, *M, FnName, {});
  Value *Result = CG.emitExpr(It, Toks.end());
  if (!Result) {
    errs() << "(compile failed, skipping)\n";
    return 0;
  }
  // "1 2" parses as 1 with junk left over. Without this check it would
  // quietly evaluate to 1 instead of telling the user the input is off.
  if (It != Toks.end() && It->K != Token::End) {
    errs() << "(trailing junk after the expression, skipping)\n";
    return 0;
  }
  // In current LLVM, ReturnInst::Create takes (Ctx, Value) or
  // (Ctx, Value, insertion point).
  BasicBlock *Entry = &M->getFunction(FnName)->getEntryBlock();
  ReturnInst::Create(*Ctx, Result, Entry);

  if (verifyModule(*M, &errs())) {
    errs() << "(IR verification failed)\n";
    return 0;
  }

  ExitOnErr(Jit.addIRModule(
      ThreadSafeModule(std::move(M), std::move(Ctx))));
  // In current ORC, lookup returns an ExecutorAddr; toPtr turns it into a
  // callable function pointer.
  auto Addr = ExitOnErr(Jit.lookup(FnName));
  auto *Fn = Addr.toPtr<int64_t (*)()>();
  return Fn();
}

// define name(p1, p2, ...) = body   -- JIT a real named function.
static void handleDefine(LLJIT &Jit, const std::string &Src) {
  std::vector<Token> Toks = lex(Src);
  auto It = Toks.begin(), End = Toks.end();

  ++It; // 'define'
  if (It == End || It->K != Token::Ident)
    return (void)(errs() << "(define: expected a name)\n");
  std::string Name = It->Text;
  ++It;
  if (It == End || It->K != Token::LP)
    return (void)(errs() << "(define: expected '(')\n");
  ++It;
  std::vector<StringRef> Params;
  while (It != End && It->K == Token::Ident) {
    Params.push_back(It->Text);
    ++It;
    if (It != End && It->K == Token::Op && It->Text == ",")
      ++It;
  }
  if (It == End || It->K != Token::RP)
    return (void)(errs() << "(define: expected ')')\n");
  ++It;
  if (It == End || It->K != Token::Op || It->Text != "=")
    return (void)(errs() << "(define: expected '=')\n");
  ++It;

  std::string FnName = Name + "." + std::to_string(DefCounter++);
  // Register before emitting the body so recursive definitions
  // (define fib(x) = ...) can call themselves.
  DefinedFuncs[Name] = {FnName, (unsigned)Params.size()};

  auto Ctx = std::make_unique<LLVMContext>();
  auto M = std::make_unique<Module>("def", *Ctx);
  CodeGen CG(*Ctx, *M, FnName, Params);
  Value *Body = CG.emitExpr(It, End);
  if (!Body) {
    DefinedFuncs.erase(Name);
    errs() << "(compile failed, skipping)\n";
    return;
  }
  if (It != End && It->K != Token::End) {
    DefinedFuncs.erase(Name);
    errs() << "(define: trailing junk after the body)\n";
    return;
  }
  BasicBlock *Entry = &M->getFunction(FnName)->getEntryBlock();
  ReturnInst::Create(*Ctx, Body, Entry);

  if (verifyModule(*M, &errs())) {
    DefinedFuncs.erase(Name);
    errs() << "(IR verification failed)\n";
    return;
  }

  ExitOnErr(Jit.addIRModule(
      ThreadSafeModule(std::move(M), std::move(Ctx))));
  std::cout << "defined " << Name << "/" << Params.size() << "\n";
}

int main(int argc, char **argv) {
  InitLLVM X(argc, argv);

  // Must register the native target: the JIT generates machine code for
  // this exact CPU. Forgetting InitializeNativeTarget() gives you a
  // confusing "no targets are registered" error.
  InitializeNativeTarget();
  InitializeNativeTargetAsmPrinter();

  // LLJIT: the batteries-included wrapper around ORC v2. It handles target
  // machine setup, object layer linking, and dynamic library search paths.
  // Next exercise: assemble those ORC layers by hand.
  auto Jit = ExitOnErr(LLJITBuilder().create());

  std::cout << "mini-jit> teaching-grade expression JIT (i64 math)\n"
            << "  1 + 2 * 3           evaluate\n"
            << "  define f(x) = ...   define a function, then call it: f(10)\n"
            << "  quit\n";

  std::string Line;
  while (true) {
    std::cout << "mini-jit> " << std::flush;
    if (!std::getline(std::cin, Line) || Line == "quit")
      break;
    if (Line.empty())
      continue;
    if (Line.rfind("define", 0) == 0) {
      handleDefine(*Jit, Line);
      continue;
    }
    int64_t R = evalTopExpression(*Jit, Line);
    std::cout << "= " << R << "\n";
  }
  return 0;
}

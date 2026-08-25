#include <memory>
#include <string>
#include <utility>
#include <vector>

class ExprAST {
public:
  virtual ~ExprAST() = default;
};

class NumberExprAST : public ExprAST {
private:
  double Val;

public:
  NumberExprAST(double Val) : Val(Val) {}
};

class BinaryExprAST : public ExprAST {
private:
  char Op;
  std::unique_ptr<ExprAST> LHS, RHS;

public:
  BinaryExprAST(char Op, std::unique_ptr<ExprAST> LHS,
                std::unique_ptr<ExprAST> RHS) :
      Op(Op), LHS(std::move(LHS)), RHS(std::move(RHS)) {}
};

class CallExprAST {
private:
  std::string Callee;
  std::vector<ExprAST> Args;

public:
  CallExprAST(const std::string &Callee, std::vector<ExprAST> Args) :
      Callee(Callee), Args(Args) {}
};

class PrototypeAST {
private:
  std::string Name;
  std::vector<std::string> Args;

public:
  PrototypeAST(std::string Name, std::vector<std::string> Args) :
      Name(Name), Args(Args) {}
  const std::string &getName() const { return Name; }
};

class FunctionAST {
  std::unique_ptr<PrototypeAST> Proto;
  std::unique_ptr<ExprAST> Body;

public:
  FunctionAST(std::unique_ptr<PrototypeAST> Proto,
              std::unique_ptr<ExprAST> Body) :
      Proto(std::move(Proto)), Body(std::move(Body)) {}
};

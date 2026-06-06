#pragma once
#include "lexer.hpp"
#include <memory>
#include <string>
#include <vector>

// ═══════════════════════════════════════════════════════════════════════
//  Forward declarations
// ═══════════════════════════════════════════════════════════════════════

struct ASTNode;
struct Expression;
struct Statement;
struct TypeNode;

using ExprPtr = std::unique_ptr<Expression>;
using StmtPtr = std::unique_ptr<Statement>;
using TypePtr = std::unique_ptr<TypeNode>;

// ═══════════════════════════════════════════════════════════════════════
//  Base nodes
// ═══════════════════════════════════════════════════════════════════════

struct ASTNode {
  SourceLocation loc;
  virtual ~ASTNode() = default;
};

struct Expression : ASTNode {
  virtual ~Expression() = default;
};

struct Statement : ASTNode {
  virtual ~Statement() = default;
};

struct TypeNode : ASTNode {
  virtual ~TypeNode() = default;
};

// ═══════════════════════════════════════════════════════════════════════
//  TYPE NODES
// ═══════════════════════════════════════════════════════════════════════

struct PrimitiveType : TypeNode {
  std::string name; // int, i32, f64, bool, char, str, void, etc.
  PrimitiveType(const std::string &n) : name(n) {}
};

struct PointerType : TypeNode {
  TypePtr pointee;
  PointerType(TypePtr p) : pointee(std::move(p)) {}
};

struct ReferenceType : TypeNode {
  TypePtr referent;
  ReferenceType(TypePtr r) : referent(std::move(r)) {}
};

struct ArrayType : TypeNode {
  TypePtr elementType;
  ExprPtr size; // nullptr = unsized
  ArrayType(TypePtr et, ExprPtr sz = nullptr)
      : elementType(std::move(et)), size(std::move(sz)) {}
};

struct NamedType : TypeNode {
  std::string name;
  std::vector<TypePtr> typeArgs; // for generic instantiation, e.g., Vec<int>
  NamedType(const std::string &n) : name(n) {}
};

// ═══════════════════════════════════════════════════════════════════════
//  EXPRESSION NODES
// ═══════════════════════════════════════════════════════════════════════

struct IntegerLiteral : Expression {
  int64_t value;
  IntegerLiteral(int64_t v) : value(v) {}
};

struct FloatLiteral : Expression {
  double value;
  FloatLiteral(double v) : value(v) {}
};

struct StringLiteral : Expression {
  std::string value;
  StringLiteral(const std::string &v) : value(v) {}
};

struct CharLiteral : Expression {
  char value;
  CharLiteral(char v) : value(v) {}
};

struct BoolLiteral : Expression {
  bool value;
  BoolLiteral(bool v) : value(v) {}
};

struct NullLiteral : Expression {};

struct IdentifierExpr : Expression {
  std::string name;
  IdentifierExpr(const std::string &n) : name(n) {}
};

struct BinaryExpr : Expression {
  TokenType op;
  ExprPtr left;
  ExprPtr right;
  BinaryExpr(TokenType o, ExprPtr l, ExprPtr r)
      : op(o), left(std::move(l)), right(std::move(r)) {}
};

struct UnaryExpr : Expression {
  TokenType op;
  ExprPtr operand;
  bool prefix; // true = prefix (!x, -x, *x, &x), false = postfix (x++, x--)
  UnaryExpr(TokenType o, ExprPtr e, bool pfx = true)
      : op(o), operand(std::move(e)), prefix(pfx) {}
};

struct CallExpr : Expression {
  ExprPtr callee;
  std::vector<ExprPtr> args;
  CallExpr(ExprPtr c, std::vector<ExprPtr> a)
      : callee(std::move(c)), args(std::move(a)) {}
};

struct MemberAccessExpr : Expression {
  ExprPtr object;
  std::string member;
  bool isArrow; // true = ->, false = .
  MemberAccessExpr(ExprPtr obj, const std::string &m, bool arrow = false)
      : object(std::move(obj)), member(m), isArrow(arrow) {}
};

struct ArraySubscriptExpr : Expression {
  ExprPtr array;
  ExprPtr index;
  ArraySubscriptExpr(ExprPtr a, ExprPtr i)
      : array(std::move(a)), index(std::move(i)) {}
};

struct CastExpr : Expression {
  ExprPtr expr;
  TypePtr targetType;
  CastExpr(ExprPtr e, TypePtr t)
      : expr(std::move(e)), targetType(std::move(t)) {}
};

struct SizeofExpr : Expression {
  TypePtr type;
  SizeofExpr(TypePtr t) : type(std::move(t)) {}
};

struct TernaryExpr : Expression {
  ExprPtr condition;
  ExprPtr thenExpr;
  ExprPtr elseExpr;
  TernaryExpr(ExprPtr c, ExprPtr t, ExprPtr e)
      : condition(std::move(c)), thenExpr(std::move(t)),
        elseExpr(std::move(e)) {}
};

struct AssignmentExpr : Expression {
  TokenType op; // EQUAL, PLUS_EQUAL, etc.
  ExprPtr target;
  ExprPtr value;
  AssignmentExpr(TokenType o, ExprPtr t, ExprPtr v)
      : op(o), target(std::move(t)), value(std::move(v)) {}
};

struct ArrayLiteralExpr : Expression {
  std::vector<ExprPtr> elements;
  ArrayLiteralExpr(std::vector<ExprPtr> elems) : elements(std::move(elems)) {}
};

struct NewExpr : Expression {
  TypePtr type;
  std::vector<ExprPtr> args;
  NewExpr(TypePtr t, std::vector<ExprPtr> a)
      : type(std::move(t)), args(std::move(a)) {}
};

struct DeleteExpr : Expression {
  ExprPtr expr;
  DeleteExpr(ExprPtr e) : expr(std::move(e)) {}
};

struct ScopeResolutionExpr : Expression {
  std::string scope;
  std::string member;
  ScopeResolutionExpr(const std::string &s, const std::string &m)
      : scope(s), member(m) {}
};

// ═══════════════════════════════════════════════════════════════════════
//  STATEMENT NODES
// ═══════════════════════════════════════════════════════════════════════

struct BlockStmt : Statement {
  std::vector<StmtPtr> statements;
  BlockStmt(std::vector<StmtPtr> stmts) : statements(std::move(stmts)) {}
};

// let Type->name = value;  OR  mut Type->name = value;
struct VariableDecl : Statement {
  std::string name;
  TypePtr type;   // may be nullptr for type inference
  ExprPtr init;   // may be nullptr for uninitialized
  bool isMutable; // mut vs let
  bool isConst;
  bool isStatic;
  bool isGlobal; // true for top-level variables
  VariableDecl(const std::string &n, TypePtr t, ExprPtr i, bool mut = false,
               bool cst = false, bool stc = false, bool glb = false)
      : name(n), type(std::move(t)), init(std::move(i)), isMutable(mut),
        isConst(cst), isStatic(stc), isGlobal(glb) {}
};

struct ExpressionStmt : Statement {
  ExprPtr expr;
  ExpressionStmt(ExprPtr e) : expr(std::move(e)) {}
};

struct ReturnStmt : Statement {
  ExprPtr value; // nullptr = void return
  ReturnStmt(ExprPtr v = nullptr) : value(std::move(v)) {}
};

struct IfStmt : Statement {
  ExprPtr condition;
  StmtPtr thenBranch;
  StmtPtr elseBranch; // nullptr if no else
  IfStmt(ExprPtr c, StmtPtr t, StmtPtr e = nullptr)
      : condition(std::move(c)), thenBranch(std::move(t)),
        elseBranch(std::move(e)) {}
};

struct WhileStmt : Statement {
  ExprPtr condition;
  StmtPtr body;
  WhileStmt(ExprPtr c, StmtPtr b)
      : condition(std::move(c)), body(std::move(b)) {}
};

struct ForStmt : Statement {
  StmtPtr init; // variable decl or expression stmt
  ExprPtr condition;
  ExprPtr increment;
  StmtPtr body;
  ForStmt(StmtPtr i, ExprPtr c, ExprPtr inc, StmtPtr b)
      : init(std::move(i)), condition(std::move(c)), increment(std::move(inc)),
        body(std::move(b)) {}
};

struct BreakStmt : Statement {};
struct ContinueStmt : Statement {};

struct OutputStmt : Statement {
  ExprPtr expr;
  OutputStmt(ExprPtr e) : expr(std::move(e)) {}
};

struct InputStmt : Statement {
  std::string varName;
  InputStmt(const std::string &v) : varName(v) {}
};

// input() as an expression — returns int from stdin via scanf
struct InputExpr : Expression {};

struct DoWhileStmt : Statement {
  StmtPtr body;
  ExprPtr condition;
  DoWhileStmt(StmtPtr b, ExprPtr c)
      : body(std::move(b)), condition(std::move(c)) {}
};

struct CaseClause {
  ExprPtr value; // nullptr = default case
  std::vector<StmtPtr> body;
};

struct SwitchStmt : Statement {
  ExprPtr expr;
  std::vector<CaseClause> cases;
  SwitchStmt(ExprPtr e, std::vector<CaseClause> c)
      : expr(std::move(e)), cases(std::move(c)) {}
};

// ═══════════════════════════════════════════════════════════════════════
//  TOP-LEVEL DECLARATIONS
// ═══════════════════════════════════════════════════════════════════════

struct Parameter {
  std::string name;
  TypePtr type;
  bool isMutable = false;
  ExprPtr defaultValue = nullptr; // for default parameter values
};

struct FunctionDecl : Statement {
  std::string name;
  std::vector<Parameter> params;
  TypePtr returnType;
  std::unique_ptr<BlockStmt> body;
  bool isPublic = true;
  bool isExtern = false;

  FunctionDecl(const std::string &n, std::vector<Parameter> p, TypePtr ret,
               std::unique_ptr<BlockStmt> b)
      : name(n), params(std::move(p)), returnType(std::move(ret)),
        body(std::move(b)) {}
};

struct StructField {
  std::string name;
  TypePtr type;
  bool isPublic = true;
};

struct StructDecl : Statement {
  std::string name;
  std::vector<StructField> fields;
  StructDecl(const std::string &n, std::vector<StructField> f)
      : name(n), fields(std::move(f)) {}
};

struct EnumVariant {
  std::string name;
  int value = -1; // -1 = auto
};

struct EnumDecl : Statement {
  std::string name;
  std::vector<EnumVariant> variants;
  EnumDecl(const std::string &n, std::vector<EnumVariant> v)
      : name(n), variants(std::move(v)) {}
};

struct NamespaceDecl : Statement {
  std::string name;
  std::vector<StmtPtr> body;
  NamespaceDecl(const std::string &n, std::vector<StmtPtr> b)
      : name(n), body(std::move(b)) {}
};

struct ImportDecl : Statement {
  std::string path; // e.g., "std::io" or "mymodule"
  ImportDecl(const std::string &p) : path(p) {}
};

struct TypeAliasDecl : Statement {
  std::string name;
  TypePtr type;
  TypeAliasDecl(const std::string &n, TypePtr t)
      : name(n), type(std::move(t)) {}
};

struct ImplBlock : Statement {
  std::string structName;
  std::vector<std::unique_ptr<FunctionDecl>> methods;
  ImplBlock(const std::string &name) : structName(name) {}
};

// ═══════════════════════════════════════════════════════════════════════
//  PROGRAM (Root node)
// ═══════════════════════════════════════════════════════════════════════

struct Program : ASTNode {
  std::vector<StmtPtr> declarations;
};
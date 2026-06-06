#pragma once
#include "ast.hpp"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// ─── Type representation for semantic analysis ─────────────────────────

enum class AstraTypeKind {
  Void,
  Bool,
  Char,
  I8,
  I16,
  I32,
  I64,
  U8,
  U16,
  U32,
  U64,
  F32,
  F64,
  Str, // string (pointer to char array)
  Pointer,
  Reference,
  Array,
  Struct,
  Enum,
  Function,
  Unknown,
};

struct AstraType {
  AstraTypeKind kind;
  std::string name;                   // for named types (struct, enum)
  std::shared_ptr<AstraType> pointee; // for pointer/reference/array
  int arraySize = 0;                  // for fixed-size arrays

  // For function types
  std::shared_ptr<AstraType> returnType;
  std::vector<std::shared_ptr<AstraType>> paramTypes;

  bool isNumeric() const;
  bool isInteger() const;
  bool isFloat() const;
  bool isSigned() const;
  bool isCompatibleWith(const AstraType &other) const;
  std::string toString() const;
};

using AstraTypePtr = std::shared_ptr<AstraType>;

// ─── Symbol table ──────────────────────────────────────────────────────

struct Symbol {
  std::string name;
  AstraTypePtr type;
  bool isMutable;
  bool isFunction;
};

class Scope {
public:
  explicit Scope(std::shared_ptr<Scope> parent = nullptr);
  bool define(const std::string &name, Symbol sym);
  Symbol *lookup(const std::string &name);
  Symbol *lookupLocal(const std::string &name);
  std::shared_ptr<Scope> parent() const { return parent_; }

private:
  std::shared_ptr<Scope> parent_;
  std::unordered_map<std::string, Symbol> symbols_;
};

// ─── Struct layout info ────────────────────────────────────────────────

struct StructInfo {
  std::string name;
  std::vector<std::pair<std::string, AstraTypePtr>> fields;
};

// ─── Enum info ─────────────────────────────────────────────────────────

struct EnumInfo {
  std::string name;
  std::unordered_map<std::string, int> variants;
};

// ─── Semantic analyzer ────────────────────────────────────────────────

class SemanticAnalyzer {
public:
  SemanticAnalyzer();
  void analyze(Program *program);

  // Accessors for codegen
  const std::unordered_map<std::string, StructInfo> &structs() const {
    return structs_;
  }
  const std::unordered_map<std::string, EnumInfo> &enums() const {
    return enums_;
  }

private:
  std::shared_ptr<Scope> currentScope_;
  std::unordered_map<std::string, StructInfo> structs_;
  std::unordered_map<std::string, EnumInfo> enums_;
  std::unordered_map<std::string, TypeNode *> typeAliases_; // using Name = Type
  AstraTypePtr currentFunctionReturnType_;
  int loopDepth_ = 0; // for break/continue validation

  // ── Type resolution ────────────────────────────────────────────────
  AstraTypePtr resolveType(TypeNode *typeNode);
  AstraTypePtr typeFromName(const std::string &name);

  // ── Visitors ───────────────────────────────────────────────────────
  void analyzeStmt(Statement *stmt);
  void analyzeBlock(BlockStmt *block);
  void analyzeVarDecl(VariableDecl *decl);
  void analyzeFuncDecl(FunctionDecl *decl);
  void analyzeStructDecl(StructDecl *decl);
  void analyzeEnumDecl(EnumDecl *decl);
  void analyzeIfStmt(IfStmt *stmt);
  void analyzeWhileStmt(WhileStmt *stmt);
  void analyzeForStmt(ForStmt *stmt);
  void analyzeDoWhileStmt(DoWhileStmt *stmt);
  void analyzeSwitchStmt(SwitchStmt *stmt);

  AstraTypePtr analyzeExpr(Expression *expr);
  AstraTypePtr analyzeBinaryExpr(BinaryExpr *expr);
  AstraTypePtr analyzeUnaryExpr(UnaryExpr *expr);
  AstraTypePtr analyzeCallExpr(CallExpr *expr);

  // ── Helpers ────────────────────────────────────────────────────────
  void pushScope();
  void popScope();
  [[noreturn]] void error(const SourceLocation &loc, const std::string &msg);
};
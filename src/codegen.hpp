#pragma once
#include "ast.hpp"
#include "semantic.hpp"

#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Value.h>
#include <llvm/IR/Verifier.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/TargetParser/Host.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class CodeGen {
public:
  CodeGen(const std::string &moduleName = "astra_module");
  ~CodeGen() = default;

  // ── Main entry ─────────────────────────────────────────────────────
  void generate(Program *program, const SemanticAnalyzer &sema);

  // ── Output ─────────────────────────────────────────────────────────
  void dumpIR() const;
  std::string getIRString() const;
  bool emitObjectFile(const std::string &filename);

  llvm::Module &getModule() { return *module_; }

private:
  std::unique_ptr<llvm::LLVMContext> context_;
  std::unique_ptr<llvm::Module> module_;
  std::unique_ptr<llvm::IRBuilder<>> builder_;

  // ── Symbol tables (LLVM values) ────────────────────────────────────
  struct VarInfo {
    llvm::AllocaInst *alloca;
    llvm::Type *type;
    bool isMutable;
    llvm::Type *pointeeType = nullptr; // for pointer variables: the type being pointed to
  };
  // For global variables: maps name -> { GlobalVariable*, type, isMutable }
  struct GlobalVarInfo {
    llvm::GlobalVariable *global;
    llvm::Type *type;
    bool isMutable;
    llvm::Type *pointeeType = nullptr;
  };
  std::vector<std::unordered_map<std::string, VarInfo>> scopes_;
  std::unordered_map<std::string, GlobalVarInfo> globals_;

  std::unordered_map<std::string, llvm::Function *> functions_;
  std::unordered_map<std::string, llvm::StructType *> structTypes_;
  std::unordered_map<std::string, std::vector<std::string>> structFieldNames_;
  std::unordered_map<std::string, std::unordered_map<std::string, int>>
      enumValues_;

  const SemanticAnalyzer *sema_ = nullptr;

  // Maps function name to its AST declaration (for default param lookup)
  std::unordered_map<std::string, FunctionDecl *> funcDecls_;

  // Type aliases: using Name = Type
  std::unordered_map<std::string, TypeNode *> typeAliases_;

  // Method table: structName -> { methodName -> mangledName }
  std::unordered_map<std::string, std::unordered_map<std::string, std::string>> methodTable_;

  // Loop break/continue targets
  std::vector<llvm::BasicBlock *> breakTargets_;
  std::vector<llvm::BasicBlock *> continueTargets_;

  // ── Type mapping ───────────────────────────────────────────────────
  llvm::Type *getLLVMType(TypeNode *typeNode);
  llvm::Type *getLLVMTypeFromAstra(const AstraType &astraTy);

  // ── Scope management ───────────────────────────────────────────────
  void pushScope();
  void popScope();
  void defineVar(const std::string &name, llvm::AllocaInst *alloca,
                 llvm::Type *ty, bool mut);
  VarInfo *lookupVar(const std::string &name);

  // ── Declaration codegen ────────────────────────────────────────────
  void codegenTopLevel(Statement *stmt);
  void codegenFunction(FunctionDecl *fn);
  void codegenStruct(StructDecl *decl);
  void codegenEnum(EnumDecl *decl);

  // ── Statement codegen ──────────────────────────────────────────────
  void codegenStmt(Statement *stmt);
  void codegenBlock(BlockStmt *block);
  void codegenVarDecl(VariableDecl *decl);
  void codegenIfStmt(IfStmt *stmt);
  void codegenWhileStmt(WhileStmt *stmt);
  void codegenForStmt(ForStmt *stmt);
  void codegenDoWhileStmt(DoWhileStmt *stmt);
  void codegenSwitchStmt(SwitchStmt *stmt);
  void codegenReturnStmt(ReturnStmt *stmt);
  void codegenOutputStmt(OutputStmt *stmt);
  void codegenGlobalVar(VariableDecl *decl);

  // ── Expression codegen ─────────────────────────────────────────────
  llvm::Value *codegenExpr(Expression *expr);
  llvm::Value *codegenBinary(BinaryExpr *expr);
  llvm::Value *codegenUnary(UnaryExpr *expr);
  llvm::Value *codegenCall(CallExpr *expr);
  llvm::Value *codegenMemberAccess(MemberAccessExpr *expr);
  llvm::Value *codegenArraySubscript(ArraySubscriptExpr *expr);
  llvm::Value *codegenAssignment(AssignmentExpr *expr);
  llvm::Value *codegenCast(CastExpr *expr);

  // Returns address (lvalue) for assignment targets like *ptr, arr[i], obj.field
  llvm::Value *codegenLValue(Expression *expr, llvm::Type **outType = nullptr);

  // ── Helpers ────────────────────────────────────────────────────────
  llvm::AllocaInst *createEntryBlockAlloca(llvm::Function *fn,
                                           const std::string &name,
                                           llvm::Type *ty);
  llvm::Function *getOrDeclarePrintf();
  llvm::Function *getOrDeclareScanf();
  llvm::Function *getOrDeclareMalloc();
  llvm::Function *getOrDeclareFree();

  llvm::Value *toBool(llvm::Value *val);
};

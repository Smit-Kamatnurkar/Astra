#include "semantic.hpp"
#include <algorithm>
#include <sstream>
#include <stdexcept>

// ═══════════════════════════════════════════════════════════════════════
//  AstraType methods
// ═══════════════════════════════════════════════════════════════════════

bool AstraType::isInteger() const {
  return kind == AstraTypeKind::I8 || kind == AstraTypeKind::I16 ||
         kind == AstraTypeKind::I32 || kind == AstraTypeKind::I64 ||
         kind == AstraTypeKind::U8 || kind == AstraTypeKind::U16 ||
         kind == AstraTypeKind::U32 || kind == AstraTypeKind::U64 ||
         kind == AstraTypeKind::Char || kind == AstraTypeKind::Bool;
}

bool AstraType::isFloat() const {
  return kind == AstraTypeKind::F32 || kind == AstraTypeKind::F64;
}

bool AstraType::isNumeric() const { return isInteger() || isFloat(); }

bool AstraType::isSigned() const {
  return kind == AstraTypeKind::I8 || kind == AstraTypeKind::I16 ||
         kind == AstraTypeKind::I32 || kind == AstraTypeKind::I64;
}

bool AstraType::isCompatibleWith(const AstraType &other) const {
  if (kind == other.kind)
    return true;
  if (isNumeric() && other.isNumeric())
    return true; // implicit numeric conversion
  if (kind == AstraTypeKind::Pointer && other.kind == AstraTypeKind::Pointer)
    return true;
  return false;
}

std::string AstraType::toString() const {
  switch (kind) {
  case AstraTypeKind::Void:
    return "void";
  case AstraTypeKind::Bool:
    return "bool";
  case AstraTypeKind::Char:
    return "char";
  case AstraTypeKind::I8:
    return "i8";
  case AstraTypeKind::I16:
    return "i16";
  case AstraTypeKind::I32:
    return "i32";
  case AstraTypeKind::I64:
    return "i64";
  case AstraTypeKind::U8:
    return "u8";
  case AstraTypeKind::U16:
    return "u16";
  case AstraTypeKind::U32:
    return "u32";
  case AstraTypeKind::U64:
    return "u64";
  case AstraTypeKind::F32:
    return "f32";
  case AstraTypeKind::F64:
    return "f64";
  case AstraTypeKind::Str:
    return "str";
  case AstraTypeKind::Pointer:
    return "*" + (pointee ? pointee->toString() : "?");
  case AstraTypeKind::Reference:
    return "&" + (pointee ? pointee->toString() : "?");
  case AstraTypeKind::Array:
    return "[" + std::to_string(arraySize) + "]" +
           (pointee ? pointee->toString() : "?");
  case AstraTypeKind::Struct:
    return name;
  case AstraTypeKind::Enum:
    return name;
  case AstraTypeKind::Function:
    return "fn";
  case AstraTypeKind::Unknown:
    return "unknown";
  }
  return "?";
}

// ═══════════════════════════════════════════════════════════════════════
//  Scope
// ═══════════════════════════════════════════════════════════════════════

Scope::Scope(std::shared_ptr<Scope> parent) : parent_(parent) {}

bool Scope::define(const std::string &name, Symbol sym) {
  if (symbols_.count(name))
    return false;
  symbols_[name] = std::move(sym);
  return true;
}

Symbol *Scope::lookup(const std::string &name) {
  auto it = symbols_.find(name);
  if (it != symbols_.end())
    return &it->second;
  if (parent_)
    return parent_->lookup(name);
  return nullptr;
}

Symbol *Scope::lookupLocal(const std::string &name) {
  auto it = symbols_.find(name);
  if (it != symbols_.end())
    return &it->second;
  return nullptr;
}

// ═══════════════════════════════════════════════════════════════════════
//  SemanticAnalyzer
// ═══════════════════════════════════════════════════════════════════════

SemanticAnalyzer::SemanticAnalyzer()
    : currentScope_(std::make_shared<Scope>()) {}

void SemanticAnalyzer::pushScope() {
  currentScope_ = std::make_shared<Scope>(currentScope_);
}

void SemanticAnalyzer::popScope() { currentScope_ = currentScope_->parent(); }

[[noreturn]] void SemanticAnalyzer::error(const SourceLocation &loc,
                                          const std::string &msg) {
  std::ostringstream os;
  os << loc.line << ":" << loc.column << ": semantic error: " << msg;
  throw std::runtime_error(os.str());
}

// ─── Type resolution ───────────────────────────────────────────────────

AstraTypePtr SemanticAnalyzer::typeFromName(const std::string &name) {
  auto ty = std::make_shared<AstraType>();

  if (name == "void") {
    ty->kind = AstraTypeKind::Void;
  } else if (name == "bool") {
    ty->kind = AstraTypeKind::Bool;
  } else if (name == "char") {
    ty->kind = AstraTypeKind::Char;
  } else if (name == "i8") {
    ty->kind = AstraTypeKind::I8;
  } else if (name == "i16") {
    ty->kind = AstraTypeKind::I16;
  } else if (name == "i32" || name == "int") {
    ty->kind = AstraTypeKind::I32;
  } else if (name == "i64") {
    ty->kind = AstraTypeKind::I64;
  } else if (name == "u8") {
    ty->kind = AstraTypeKind::U8;
  } else if (name == "u16") {
    ty->kind = AstraTypeKind::U16;
  } else if (name == "u32") {
    ty->kind = AstraTypeKind::U32;
  } else if (name == "u64") {
    ty->kind = AstraTypeKind::U64;
  } else if (name == "f32") {
    ty->kind = AstraTypeKind::F32;
  } else if (name == "f64") {
    ty->kind = AstraTypeKind::F64;
  } else if (name == "str") {
    ty->kind = AstraTypeKind::Str;
  } else if (structs_.count(name)) {
    ty->kind = AstraTypeKind::Struct;
    ty->name = name;
  } else if (enums_.count(name)) {
    ty->kind = AstraTypeKind::Enum;
    ty->name = name;
  } else if (typeAliases_.count(name)) {
    // Resolve type alias
    return resolveType(typeAliases_[name]);
  } else {
    ty->kind = AstraTypeKind::Unknown;
    ty->name = name;
  }

  return ty;
}

AstraTypePtr SemanticAnalyzer::resolveType(TypeNode *typeNode) {
  if (!typeNode)
    return typeFromName("void");

  if (auto prim = dynamic_cast<PrimitiveType *>(typeNode)) {
    return typeFromName(prim->name);
  }
  if (auto ptr = dynamic_cast<PointerType *>(typeNode)) {
    auto ty = std::make_shared<AstraType>();
    ty->kind = AstraTypeKind::Pointer;
    ty->pointee = resolveType(ptr->pointee.get());
    return ty;
  }
  if (auto ref = dynamic_cast<ReferenceType *>(typeNode)) {
    auto ty = std::make_shared<AstraType>();
    ty->kind = AstraTypeKind::Reference;
    ty->pointee = resolveType(ref->referent.get());
    return ty;
  }
  if (auto arr = dynamic_cast<ArrayType *>(typeNode)) {
    auto ty = std::make_shared<AstraType>();
    ty->kind = AstraTypeKind::Array;
    ty->pointee = resolveType(arr->elementType.get());
    if (auto sizeExpr = dynamic_cast<IntegerLiteral *>(arr->size.get())) {
      ty->arraySize = static_cast<int>(sizeExpr->value);
    }
    return ty;
  }
  if (auto named = dynamic_cast<NamedType *>(typeNode)) {
    return typeFromName(named->name);
  }
  return typeFromName("unknown");
}

// ═══════════════════════════════════════════════════════════════════════
//  Analysis entry point
// ═══════════════════════════════════════════════════════════════════════

void SemanticAnalyzer::analyze(Program *program) {
  // First pass: register all top-level declarations (functions, structs, enums)
  for (auto &decl : program->declarations) {
    if (auto fn = dynamic_cast<FunctionDecl *>(decl.get())) {
      auto fnType = std::make_shared<AstraType>();
      fnType->kind = AstraTypeKind::Function;
      fnType->returnType = resolveType(fn->returnType.get());
      for (auto &p : fn->params) {
        fnType->paramTypes.push_back(resolveType(p.type.get()));
      }
      currentScope_->define(fn->name, {fn->name, fnType, false, true});
    }
    if (auto st = dynamic_cast<StructDecl *>(decl.get())) {
      analyzeStructDecl(st);
    }
    if (auto en = dynamic_cast<EnumDecl *>(decl.get())) {
      analyzeEnumDecl(en);
    }
  }

  // Second pass: analyze function bodies and global variable decls
  for (auto &decl : program->declarations) {
    if (auto fn = dynamic_cast<FunctionDecl *>(decl.get())) {
      analyzeFuncDecl(fn);
    }
    if (auto vd = dynamic_cast<VariableDecl *>(decl.get())) {
      analyzeVarDecl(vd);
    }
  }
}

// ─── Statement analysis ────────────────────────────────────────────────

void SemanticAnalyzer::analyzeStmt(Statement *stmt) {
  if (auto block = dynamic_cast<BlockStmt *>(stmt)) {
    pushScope();
    for (auto &s : block->statements) {
      analyzeStmt(s.get());
    }
    popScope();
    return;
  }

  if (auto typeAlias = dynamic_cast<TypeAliasDecl *>(stmt)) {
    typeAliases_[typeAlias->name] = typeAlias->type.get();
    return;
  }
  if (auto varD = dynamic_cast<VariableDecl *>(stmt)) {
    analyzeVarDecl(varD);
    return;
  }
  if (auto ifS = dynamic_cast<IfStmt *>(stmt)) {
    analyzeIfStmt(ifS);
    return;
  }
  if (auto whS = dynamic_cast<WhileStmt *>(stmt)) {
    analyzeWhileStmt(whS);
    return;
  }
  if (auto foS = dynamic_cast<ForStmt *>(stmt)) {
    analyzeForStmt(foS);
    return;
  }
  if (auto ret = dynamic_cast<ReturnStmt *>(stmt)) {
    if (ret->value)
      analyzeExpr(ret->value.get());
    return;
  }
  if (auto out = dynamic_cast<OutputStmt *>(stmt)) {
    analyzeExpr(out->expr.get());
    return;
  }
  if (auto doW = dynamic_cast<DoWhileStmt *>(stmt)) {
    analyzeDoWhileStmt(doW);
    return;
  }
  if (auto sw = dynamic_cast<SwitchStmt *>(stmt)) {
    analyzeSwitchStmt(sw);
    return;
  }
  if (auto exprS = dynamic_cast<ExpressionStmt *>(stmt)) {
    analyzeExpr(exprS->expr.get());
    return;
  }
  // break, continue — validate they are inside a loop
  if (dynamic_cast<BreakStmt *>(stmt)) {
    if (loopDepth_ <= 0)
      error(stmt->loc, "'break' outside of loop or switch");
    return;
  }
  if (dynamic_cast<ContinueStmt *>(stmt)) {
    if (loopDepth_ <= 0)
      error(stmt->loc, "'continue' outside of loop");
    return;
  }
}

void SemanticAnalyzer::analyzeBlock(BlockStmt *block) {
  pushScope();
  for (auto &s : block->statements) {
    analyzeStmt(s.get());
  }
  popScope();
}

void SemanticAnalyzer::analyzeVarDecl(VariableDecl *decl) {
  auto ty = resolveType(decl->type.get());

  if (decl->init) {
    auto initTy = analyzeExpr(decl->init.get());
    // Type compatibility check (simplified — allow numeric conversions)
  }

  if (!currentScope_->define(decl->name,
                             {decl->name, ty, decl->isMutable, false})) {
    error(decl->loc,
          "variable '" + decl->name + "' already declared in this scope");
  }
}

void SemanticAnalyzer::analyzeFuncDecl(FunctionDecl *decl) {
  pushScope();
  currentFunctionReturnType_ = resolveType(decl->returnType.get());

  // Define parameters in function scope
  for (auto &p : decl->params) {
    auto ty = resolveType(p.type.get());
    currentScope_->define(p.name, {p.name, ty, p.isMutable, false});
  }

  // Analyze body
  if (decl->body) {
    for (auto &s : decl->body->statements) {
      analyzeStmt(s.get());
    }
  }

  popScope();
}

void SemanticAnalyzer::analyzeStructDecl(StructDecl *decl) {
  StructInfo info;
  info.name = decl->name;
  for (auto &f : decl->fields) {
    auto ty = resolveType(f.type.get());
    info.fields.push_back({f.name, ty});
  }
  structs_[decl->name] = std::move(info);
}

void SemanticAnalyzer::analyzeEnumDecl(EnumDecl *decl) {
  EnumInfo info;
  info.name = decl->name;
  int nextVal = 0;
  for (auto &v : decl->variants) {
    int val = v.value >= 0 ? v.value : nextVal;
    info.variants[v.name] = val;
    nextVal = val + 1;
  }
  enums_[decl->name] = std::move(info);
}

void SemanticAnalyzer::analyzeIfStmt(IfStmt *stmt) {
  analyzeExpr(stmt->condition.get());
  analyzeStmt(stmt->thenBranch.get());
  if (stmt->elseBranch)
    analyzeStmt(stmt->elseBranch.get());
}

void SemanticAnalyzer::analyzeWhileStmt(WhileStmt *stmt) {
  analyzeExpr(stmt->condition.get());
  loopDepth_++;
  analyzeStmt(stmt->body.get());
  loopDepth_--;
}

void SemanticAnalyzer::analyzeForStmt(ForStmt *stmt) {
  pushScope();
  if (stmt->init)
    analyzeStmt(stmt->init.get());
  if (stmt->condition)
    analyzeExpr(stmt->condition.get());
  if (stmt->increment)
    analyzeExpr(stmt->increment.get());
  loopDepth_++;
  analyzeStmt(stmt->body.get());
  loopDepth_--;
  popScope();
}

void SemanticAnalyzer::analyzeDoWhileStmt(DoWhileStmt *stmt) {
  loopDepth_++;
  analyzeStmt(stmt->body.get());
  loopDepth_--;
  analyzeExpr(stmt->condition.get());
}

void SemanticAnalyzer::analyzeSwitchStmt(SwitchStmt *stmt) {
  analyzeExpr(stmt->expr.get());
  loopDepth_++; // allow break inside switch
  for (auto &clause : stmt->cases) {
    if (clause.value)
      analyzeExpr(clause.value.get());
    for (auto &s : clause.body) {
      analyzeStmt(s.get());
    }
  }
  loopDepth_--;
}

// ═══════════════════════════════════════════════════════════════════════
//  Expression analysis (returns inferred type)
// ═══════════════════════════════════════════════════════════════════════

AstraTypePtr SemanticAnalyzer::analyzeExpr(Expression *expr) {
  if (auto intLit = dynamic_cast<IntegerLiteral *>(expr))
    return typeFromName("i32");
  if (auto floatLit = dynamic_cast<FloatLiteral *>(expr))
    return typeFromName("f64");
  if (auto strLit = dynamic_cast<StringLiteral *>(expr))
    return typeFromName("str");
  if (auto charLit = dynamic_cast<CharLiteral *>(expr))
    return typeFromName("char");
  if (auto boolLit = dynamic_cast<BoolLiteral *>(expr))
    return typeFromName("bool");
  if (auto nullLit = dynamic_cast<NullLiteral *>(expr)) {
    auto ty = std::make_shared<AstraType>();
    ty->kind = AstraTypeKind::Pointer;
    ty->pointee = typeFromName("void");
    return ty;
  }

  if (auto ident = dynamic_cast<IdentifierExpr *>(expr)) {
    auto sym = currentScope_->lookup(ident->name);
    if (!sym)
      error(expr->loc, "undeclared identifier '" + ident->name + "'");
    return sym->type;
  }

  if (auto bin = dynamic_cast<BinaryExpr *>(expr))
    return analyzeBinaryExpr(bin);

  if (auto un = dynamic_cast<UnaryExpr *>(expr))
    return analyzeUnaryExpr(un);

  if (auto call = dynamic_cast<CallExpr *>(expr))
    return analyzeCallExpr(call);

  if (auto assign = dynamic_cast<AssignmentExpr *>(expr)) {
    auto targetTy = analyzeExpr(assign->target.get());
    analyzeExpr(assign->value.get());
    return targetTy;
  }

  if (auto member = dynamic_cast<MemberAccessExpr *>(expr)) {
    auto objTy = analyzeExpr(member->object.get());
    // Simplified: return i32 for unknown member types
    if (objTy->kind == AstraTypeKind::Struct) {
      auto it = structs_.find(objTy->name);
      if (it != structs_.end()) {
        for (auto &[fname, ftype] : it->second.fields) {
          if (fname == member->member)
            return ftype;
        }
      }
    }
    return typeFromName("i32"); // fallback
  }

  if (auto subscript = dynamic_cast<ArraySubscriptExpr *>(expr)) {
    auto arrTy = analyzeExpr(subscript->array.get());
    analyzeExpr(subscript->index.get());
    if (arrTy->pointee)
      return arrTy->pointee;
    return typeFromName("i32"); // fallback
  }

  if (auto cast = dynamic_cast<CastExpr *>(expr)) {
    analyzeExpr(cast->expr.get());
    return resolveType(cast->targetType.get());
  }

  if (auto sizeOf = dynamic_cast<SizeofExpr *>(expr))
    return typeFromName("i64");

  if (auto ternary = dynamic_cast<TernaryExpr *>(expr)) {
    analyzeExpr(ternary->condition.get());
    auto thenTy = analyzeExpr(ternary->thenExpr.get());
    analyzeExpr(ternary->elseExpr.get());
    return thenTy;
  }

  if (auto arrLit = dynamic_cast<ArrayLiteralExpr *>(expr)) {
    AstraTypePtr elemTy = typeFromName("i32");
    if (!arrLit->elements.empty()) {
      elemTy = analyzeExpr(arrLit->elements[0].get());
    }
    auto ty = std::make_shared<AstraType>();
    ty->kind = AstraTypeKind::Array;
    ty->pointee = elemTy;
    ty->arraySize = static_cast<int>(arrLit->elements.size());
    return ty;
  }

  if (auto scopeRes = dynamic_cast<ScopeResolutionExpr *>(expr)) {
    // Check if it's an enum variant
    auto it = enums_.find(scopeRes->scope);
    if (it != enums_.end()) {
      if (it->second.variants.count(scopeRes->member)) {
        return typeFromName(scopeRes->scope);
      }
    }
    return typeFromName("i32"); // fallback
  }

  if (dynamic_cast<InputExpr *>(expr)) {
    return typeFromName("i32"); // input() returns int
  }

  return typeFromName("unknown");
}

AstraTypePtr SemanticAnalyzer::analyzeBinaryExpr(BinaryExpr *expr) {
  auto leftTy = analyzeExpr(expr->left.get());
  auto rightTy = analyzeExpr(expr->right.get());

  // Pointer arithmetic type checking
  if (leftTy->kind == AstraTypeKind::Pointer || rightTy->kind == AstraTypeKind::Pointer) {
    // ptr + int or int + ptr → pointer
    if (expr->op == TokenType::PLUS) {
      if (leftTy->kind == AstraTypeKind::Pointer && rightTy->isInteger())
        return leftTy;
      if (rightTy->kind == AstraTypeKind::Pointer && leftTy->isInteger())
        return rightTy;
    }
    // ptr - int → pointer, ptr - ptr → i64
    if (expr->op == TokenType::MINUS) {
      if (leftTy->kind == AstraTypeKind::Pointer && rightTy->isInteger())
        return leftTy;
      if (leftTy->kind == AstraTypeKind::Pointer && rightTy->kind == AstraTypeKind::Pointer)
        return typeFromName("i64");
    }
    // Pointer comparison
    if (expr->op == TokenType::EQUAL_EQUAL || expr->op == TokenType::EXCLAIM_EQUAL)
      return typeFromName("bool");
  }

  // Comparison operators return bool
  if (expr->op == TokenType::EQUAL_EQUAL ||
      expr->op == TokenType::EXCLAIM_EQUAL || expr->op == TokenType::LESS ||
      expr->op == TokenType::GREATER || expr->op == TokenType::LESS_EQUAL ||
      expr->op == TokenType::GREATER_EQUAL ||
      expr->op == TokenType::AMPERSAND_AMPERSAND ||
      expr->op == TokenType::PIPE_PIPE) {
    return typeFromName("bool");
  }

  // If either is float, result is float
  if (leftTy->isFloat() || rightTy->isFloat()) {
    return typeFromName("f64");
  }

  return leftTy; // default to left operand type
}

AstraTypePtr SemanticAnalyzer::analyzeUnaryExpr(UnaryExpr *expr) {
  auto operandTy = analyzeExpr(expr->operand.get());

  if (expr->op == TokenType::EXCLAIM)
    return typeFromName("bool");
  if (expr->op == TokenType::AMPERSAND) {
    auto ty = std::make_shared<AstraType>();
    ty->kind = AstraTypeKind::Pointer;
    ty->pointee = operandTy;
    return ty;
  }
  if (expr->op == TokenType::STAR) {
    if (operandTy->pointee)
      return operandTy->pointee;
    return typeFromName("i32"); // fallback
  }

  return operandTy;
}

AstraTypePtr SemanticAnalyzer::analyzeCallExpr(CallExpr *expr) {
  auto calleeTy = analyzeExpr(expr->callee.get());
  for (auto &arg : expr->args) {
    analyzeExpr(arg.get());
  }
  if (calleeTy->kind == AstraTypeKind::Function && calleeTy->returnType) {
    return calleeTy->returnType;
  }
  return typeFromName("i32"); // fallback for unresolved
}
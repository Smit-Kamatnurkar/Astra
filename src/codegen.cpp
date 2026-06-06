#include "codegen.hpp"
#include <iostream>
#include <sstream>
#include <stdexcept>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

// ═══════════════════════════════════════════════════════════════════════
//  Constructor
// ═══════════════════════════════════════════════════════════════════════

CodeGen::CodeGen(const std::string &moduleName) {
  context_ = std::make_unique<llvm::LLVMContext>();
  module_ = std::make_unique<llvm::Module>(moduleName, *context_);
  builder_ = std::make_unique<llvm::IRBuilder<>>(*context_);
}

// ═══════════════════════════════════════════════════════════════════════
//  Scope management
// ═══════════════════════════════════════════════════════════════════════

void CodeGen::pushScope() { scopes_.emplace_back(); }
void CodeGen::popScope() { scopes_.pop_back(); }

void CodeGen::defineVar(const std::string &name, llvm::AllocaInst *alloca,
                        llvm::Type *ty, bool mut) {
  scopes_.back()[name] = {alloca, ty, mut};
}

CodeGen::VarInfo *CodeGen::lookupVar(const std::string &name) {
  for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
    auto found = it->find(name);
    if (found != it->end())
      return &found->second;
  }
  return nullptr;
}

// ═══════════════════════════════════════════════════════════════════════
//  Type mapping  —  Astra type AST → LLVM type
// ═══════════════════════════════════════════════════════════════════════

llvm::Type *CodeGen::getLLVMType(TypeNode *typeNode) {
  if (!typeNode)
    return llvm::Type::getVoidTy(*context_);

  if (auto prim = dynamic_cast<PrimitiveType *>(typeNode)) {
    // Resolve type aliases first
    auto aliasIt = typeAliases_.find(prim->name);
    if (aliasIt != typeAliases_.end()) {
      return getLLVMType(aliasIt->second);
    }
    const auto &n = prim->name;
    if (n == "void")
      return llvm::Type::getVoidTy(*context_);
    if (n == "bool")
      return llvm::Type::getInt1Ty(*context_);
    if (n == "char" || n == "i8" || n == "u8")
      return llvm::Type::getInt8Ty(*context_);
    if (n == "i16" || n == "u16")
      return llvm::Type::getInt16Ty(*context_);
    if (n == "int" || n == "i32" || n == "u32")
      return llvm::Type::getInt32Ty(*context_);
    if (n == "i64" || n == "u64")
      return llvm::Type::getInt64Ty(*context_);
    if (n == "f32")
      return llvm::Type::getFloatTy(*context_);
    if (n == "f64")
      return llvm::Type::getDoubleTy(*context_);
    if (n == "str")
      return llvm::PointerType::getUnqual(llvm::Type::getInt8Ty(*context_));

    // Named type (struct, etc.)
    auto sit = structTypes_.find(n);
    if (sit != structTypes_.end())
      return sit->second;

    // Enum is i32
    if (sema_ && sema_->enums().count(n))
      return llvm::Type::getInt32Ty(*context_);

    throw std::runtime_error("unknown type: " + n);
  }

  if (auto ptr = dynamic_cast<PointerType *>(typeNode)) {
    return llvm::PointerType::getUnqual(getLLVMType(ptr->pointee.get()));
  }

  if (auto ref = dynamic_cast<ReferenceType *>(typeNode)) {
    return llvm::PointerType::getUnqual(getLLVMType(ref->referent.get()));
  }

  if (auto arr = dynamic_cast<ArrayType *>(typeNode)) {
    auto elemTy = getLLVMType(arr->elementType.get());
    uint64_t sz = 0;
    if (auto sizeExpr = dynamic_cast<IntegerLiteral *>(arr->size.get()))
      sz = sizeExpr->value;
    return llvm::ArrayType::get(elemTy, sz);
  }

  if (auto named = dynamic_cast<NamedType *>(typeNode)) {
    auto sit = structTypes_.find(named->name);
    if (sit != structTypes_.end())
      return sit->second;
    if (sema_ && sema_->enums().count(named->name))
      return llvm::Type::getInt32Ty(*context_);
    throw std::runtime_error("unknown named type: " + named->name);
  }

  throw std::runtime_error("cannot map type to LLVM");
}

// ═══════════════════════════════════════════════════════════════════════
//  Helper: create alloca in function entry block
// ═══════════════════════════════════════════════════════════════════════

llvm::AllocaInst *CodeGen::createEntryBlockAlloca(llvm::Function *fn,
                                                  const std::string &name,
                                                  llvm::Type *ty) {
  llvm::IRBuilder<> tmpB(&fn->getEntryBlock(), fn->getEntryBlock().begin());
  return tmpB.CreateAlloca(ty, nullptr, name);
}

// ═══════════════════════════════════════════════════════════════════════
//  Standard library declarations
// ═══════════════════════════════════════════════════════════════════════

llvm::Function *CodeGen::getOrDeclarePrintf() {
  if (auto f = module_->getFunction("printf"))
    return f;
  auto ty = llvm::FunctionType::get(
      llvm::Type::getInt32Ty(*context_),
      {llvm::PointerType::getUnqual(llvm::Type::getInt8Ty(*context_))}, true);
  return llvm::Function::Create(ty, llvm::Function::ExternalLinkage, "printf",
                                module_.get());
}

llvm::Function *CodeGen::getOrDeclareScanf() {
  if (auto f = module_->getFunction("scanf"))
    return f;
  auto ty = llvm::FunctionType::get(
      llvm::Type::getInt32Ty(*context_),
      {llvm::PointerType::getUnqual(llvm::Type::getInt8Ty(*context_))}, true);
  return llvm::Function::Create(ty, llvm::Function::ExternalLinkage, "scanf",
                                module_.get());
}

llvm::Function *CodeGen::getOrDeclareMalloc() {
  if (auto f = module_->getFunction("malloc"))
    return f;
  auto ty = llvm::FunctionType::get(
      llvm::PointerType::getUnqual(llvm::Type::getInt8Ty(*context_)),
      {llvm::Type::getInt64Ty(*context_)}, false);
  return llvm::Function::Create(ty, llvm::Function::ExternalLinkage, "malloc",
                                module_.get());
}

llvm::Function *CodeGen::getOrDeclareFree() {
  if (auto f = module_->getFunction("free"))
    return f;
  auto ty = llvm::FunctionType::get(
      llvm::Type::getVoidTy(*context_),
      {llvm::PointerType::getUnqual(llvm::Type::getInt8Ty(*context_))}, false);
  return llvm::Function::Create(ty, llvm::Function::ExternalLinkage, "free",
                                module_.get());
}

// ═══════════════════════════════════════════════════════════════════════
//  Main code generation entry
// ═══════════════════════════════════════════════════════════════════════

void CodeGen::generate(Program *program, const SemanticAnalyzer &sema) {
  sema_ = &sema;

  // First pass: declare structs, enums, and type aliases
  for (auto &decl : program->declarations) {
    if (auto sd = dynamic_cast<StructDecl *>(decl.get()))
      codegenStruct(sd);
    if (auto ed = dynamic_cast<EnumDecl *>(decl.get()))
      codegenEnum(ed);
    if (auto ta = dynamic_cast<TypeAliasDecl *>(decl.get())) {
      typeAliases_[ta->name] = ta->type.get();
    }
  }

  // Second pass: emit global variables
  for (auto &decl : program->declarations) {
    if (auto vd = dynamic_cast<VariableDecl *>(decl.get())) {
      if (vd->isGlobal)
        codegenGlobalVar(vd);
    }
  }

  // Third pass: declare all functions (forward declarations)
  for (auto &decl : program->declarations) {
    if (auto fn = dynamic_cast<FunctionDecl *>(decl.get())) {
      // Build function type
      auto retTy = getLLVMType(fn->returnType.get());
      std::vector<llvm::Type *> paramTys;
      for (auto &p : fn->params) {
        paramTys.push_back(getLLVMType(p.type.get()));
      }
      auto fnTy = llvm::FunctionType::get(retTy, paramTys, false);
      auto llvmFn = llvm::Function::Create(
          fnTy, llvm::Function::ExternalLinkage, fn->name, module_.get());

      // Name parameters
      size_t idx = 0;
      for (auto &arg : llvmFn->args()) {
        arg.setName(fn->params[idx++].name);
      }
      functions_[fn->name] = llvmFn;
      funcDecls_[fn->name] = fn; // track AST decl for default param lookup
    }
    // Process impl blocks: declare methods as StructName_methodName
    if (auto impl = dynamic_cast<ImplBlock *>(decl.get())) {
      auto structName = impl->structName;
      auto structTyIt = structTypes_.find(structName);
      if (structTyIt == structTypes_.end())
        throw std::runtime_error("impl for unknown struct: " + structName);
      auto structTy = structTyIt->second;
      auto ptrTy = llvm::PointerType::getUnqual(*context_);

      for (auto &method : impl->methods) {
        std::string mangledName = structName + "_" + method->name;
        // Build param types: self pointer + declared params
        std::vector<llvm::Type *> paramTys;
        paramTys.push_back(ptrTy); // self: pointer to struct
        for (auto &p : method->params) {
          if (p.name == "self") continue; // skip explicit self
          paramTys.push_back(getLLVMType(p.type.get()));
        }
        auto retTy = getLLVMType(method->returnType.get());
        auto fnTy = llvm::FunctionType::get(retTy, paramTys, false);
        auto llvmFn = llvm::Function::Create(
            fnTy, llvm::Function::ExternalLinkage, mangledName, module_.get());

        // Name params: first is "self", rest from declared params
        auto argIt = llvmFn->arg_begin();
        argIt->setName("self");
        argIt++;
        for (auto &p : method->params) {
          if (p.name == "self") continue;
          argIt->setName(p.name);
          argIt++;
        }

        functions_[mangledName] = llvmFn;
        funcDecls_[mangledName] = method.get();
        methodTable_[structName][method->name] = mangledName;
      }
    }
  }

  // Fourth pass: generate function bodies (skip externs)
  for (auto &decl : program->declarations) {
    if (auto fn = dynamic_cast<FunctionDecl *>(decl.get())) {
      if (!fn->isExtern && fn->body) {
        codegenFunction(fn);
      }
    }
    // Generate impl method bodies
    if (auto impl = dynamic_cast<ImplBlock *>(decl.get())) {
      auto structName = impl->structName;
      auto structTy = structTypes_[structName];
      for (auto &method : impl->methods) {
        std::string mangledName = structName + "_" + method->name;
        auto llvmFn = module_->getFunction(mangledName);
        if (!llvmFn || !method->body) continue;

        // Create entry block
        auto bb = llvm::BasicBlock::Create(*context_, "entry", llvmFn);
        builder_->SetInsertPoint(bb);
        pushScope();

        // Bind self parameter: alloca for self pointer
        auto selfArg = llvmFn->arg_begin();
        auto selfAlloca = createEntryBlockAlloca(llvmFn, "self",
                                                 llvm::PointerType::getUnqual(*context_));
        builder_->CreateStore(selfArg, selfAlloca);
        // 'self' is a pointer to the struct — store as a pointer variable
        scopes_.back()["self"] = {selfAlloca,
                                  llvm::PointerType::getUnqual(*context_),
                                  false, structTy};

        // Bind remaining parameters
        auto argIt = llvmFn->arg_begin();
        argIt++; // skip self
        for (auto &p : method->params) {
          if (p.name == "self") continue;
          auto ty = argIt->getType();
          auto alloca = createEntryBlockAlloca(llvmFn, p.name, ty);
          builder_->CreateStore(&*argIt, alloca);
          scopes_.back()[p.name] = {alloca, ty, p.isMutable, nullptr};
          argIt++;
        }

        // Generate body statements
        for (auto &s : method->body->statements) {
          codegenStmt(s.get());
          if (builder_->GetInsertBlock()->getTerminator())
            break;
        }

        // Add implicit return if needed
        if (!builder_->GetInsertBlock()->getTerminator()) {
          auto retTy = llvmFn->getReturnType();
          if (retTy->isVoidTy())
            builder_->CreateRetVoid();
          else
            builder_->CreateRet(llvm::Constant::getNullValue(retTy));
        }

        popScope();
      }
    }
  }
}

// ═══════════════════════════════════════════════════════════════════════
//  Struct codegen
// ═══════════════════════════════════════════════════════════════════════

void CodeGen::codegenStruct(StructDecl *decl) {
  std::vector<llvm::Type *> fieldTypes;
  std::vector<std::string> fieldNames;
  for (auto &f : decl->fields) {
    fieldTypes.push_back(getLLVMType(f.type.get()));
    fieldNames.push_back(f.name);
  }
  auto structTy = llvm::StructType::create(*context_, fieldTypes, decl->name);
  structTypes_[decl->name] = structTy;
  structFieldNames_[decl->name] = fieldNames;
}

// ═══════════════════════════════════════════════════════════════════════
//  Enum codegen
// ═══════════════════════════════════════════════════════════════════════

void CodeGen::codegenEnum(EnumDecl *decl) {
  std::unordered_map<std::string, int> vals;
  int next = 0;
  for (auto &v : decl->variants) {
    int val = (v.value >= 0) ? v.value : next;
    vals[v.name] = val;
    next = val + 1;
  }
  enumValues_[decl->name] = vals;
}

// ═══════════════════════════════════════════════════════════════════════
//  Function codegen
// ═══════════════════════════════════════════════════════════════════════

void CodeGen::codegenFunction(FunctionDecl *fn) {
  auto llvmFn = functions_[fn->name];
  if (!llvmFn)
    throw std::runtime_error("function not declared: " + fn->name);

  auto entry = llvm::BasicBlock::Create(*context_, "entry", llvmFn);
  builder_->SetInsertPoint(entry);

  pushScope();

  // Allocate and store parameters
  for (auto &arg : llvmFn->args()) {
    auto alloca =
        createEntryBlockAlloca(llvmFn, arg.getName().str(), arg.getType());
    builder_->CreateStore(&arg, alloca);
    defineVar(arg.getName().str(), alloca, arg.getType(), true);
  }

  // Generate body
  if (fn->body) {
    for (auto &stmt : fn->body->statements) {
      codegenStmt(stmt.get());
      // If the current block already has a terminator, stop generating
      if (builder_->GetInsertBlock()->getTerminator())
        break;
    }
  }

  // If no terminator, add implicit return
  if (!builder_->GetInsertBlock()->getTerminator()) {
    if (llvmFn->getReturnType()->isVoidTy()) {
      builder_->CreateRetVoid();
    } else {
      builder_->CreateRet(
          llvm::Constant::getNullValue(llvmFn->getReturnType()));
    }
  }

  popScope();

  // Verify function
  std::string verifyErr;
  llvm::raw_string_ostream errStream(verifyErr);
  if (llvm::verifyFunction(*llvmFn, &errStream)) {
    std::cerr << "LLVM verification failed for function " << fn->name << ":\n"
              << verifyErr << "\n";
    llvmFn->print(llvm::errs());
    throw std::runtime_error("LLVM IR verification failed for " + fn->name);
  }
}

// ═══════════════════════════════════════════════════════════════════════
//  Statement codegen
// ═══════════════════════════════════════════════════════════════════════

void CodeGen::codegenStmt(Statement *stmt) {
  if (builder_->GetInsertBlock()->getTerminator())
    return; // dead code after terminator

  if (auto block = dynamic_cast<BlockStmt *>(stmt)) {
    codegenBlock(block);
    return;
  }
  if (auto var = dynamic_cast<VariableDecl *>(stmt)) {
    codegenVarDecl(var);
    return;
  }
  if (auto ifs = dynamic_cast<IfStmt *>(stmt)) {
    codegenIfStmt(ifs);
    return;
  }
  if (auto whs = dynamic_cast<WhileStmt *>(stmt)) {
    codegenWhileStmt(whs);
    return;
  }
  if (auto frs = dynamic_cast<ForStmt *>(stmt)) {
    codegenForStmt(frs);
    return;
  }
  if (auto ret = dynamic_cast<ReturnStmt *>(stmt)) {
    codegenReturnStmt(ret);
    return;
  }
  if (auto out = dynamic_cast<OutputStmt *>(stmt)) {
    codegenOutputStmt(out);
    return;
  }

  if (auto brk = dynamic_cast<BreakStmt *>(stmt)) {
    if (!breakTargets_.empty()) {
      builder_->CreateBr(breakTargets_.back());
    }
    return;
  }

  if (auto cont = dynamic_cast<ContinueStmt *>(stmt)) {
    if (!continueTargets_.empty()) {
      builder_->CreateBr(continueTargets_.back());
    }
    return;
  }

  if (auto doW = dynamic_cast<DoWhileStmt *>(stmt)) {
    codegenDoWhileStmt(doW);
    return;
  }
  if (auto sw = dynamic_cast<SwitchStmt *>(stmt)) {
    codegenSwitchStmt(sw);
    return;
  }

  if (auto expr = dynamic_cast<ExpressionStmt *>(stmt)) {
    codegenExpr(expr->expr.get());
    return;
  }

  if (auto ta = dynamic_cast<TypeAliasDecl *>(stmt)) {
    typeAliases_[ta->name] = ta->type.get();
    return;
  }
}

void CodeGen::codegenBlock(BlockStmt *block) {
  pushScope();
  for (auto &s : block->statements) {
    codegenStmt(s.get());
    if (builder_->GetInsertBlock()->getTerminator())
      break;
  }
  popScope();
}

// ─── Variable declaration ──────────────────────────────────────────────

void CodeGen::codegenVarDecl(VariableDecl *decl) {
  auto fn = builder_->GetInsertBlock()->getParent();

  // Handle 'auto' type inference
  bool isAuto = false;
  if (auto prim = dynamic_cast<PrimitiveType *>(decl->type.get())) {
    if (prim->name == "auto") isAuto = true;
  }

  if (isAuto) {
    if (!decl->init)
      throw std::runtime_error("'auto' variable requires an initializer");
    auto val = codegenExpr(decl->init.get());
    auto ty = val->getType();
    auto alloca = createEntryBlockAlloca(fn, decl->name, ty);
    builder_->CreateStore(val, alloca);
    scopes_.back()[decl->name] = {alloca, ty, decl->isMutable, nullptr};
    return;
  }

  auto ty = getLLVMType(decl->type.get());
  auto alloca = createEntryBlockAlloca(fn, decl->name, ty);

  // Track pointee type for pointer variables
  llvm::Type *pointeeTy = nullptr;
  if (auto ptrType = dynamic_cast<PointerType *>(decl->type.get())) {
    pointeeTy = getLLVMType(ptrType->pointee.get());
  }

  if (decl->init) {
    auto val = codegenExpr(decl->init.get());
    if (val) {
      // Type coercion if needed
      if (val->getType() != ty) {
        if (ty->isIntegerTy() && val->getType()->isIntegerTy()) {
          val = builder_->CreateIntCast(val, ty, true);
        } else if (ty->isFloatingPointTy() && val->getType()->isIntegerTy()) {
          val = builder_->CreateSIToFP(val, ty);
        } else if (ty->isIntegerTy() && val->getType()->isFloatingPointTy()) {
          val = builder_->CreateFPToSI(val, ty);
        } else if (ty->isFloatingPointTy() &&
                   val->getType()->isFloatingPointTy()) {
          if (ty->getPrimitiveSizeInBits() >
              val->getType()->getPrimitiveSizeInBits())
            val = builder_->CreateFPExt(val, ty);
          else
            val = builder_->CreateFPTrunc(val, ty);
        }
      }
      builder_->CreateStore(val, alloca);
    }
  }

  scopes_.back()[decl->name] = {alloca, ty, decl->isMutable, pointeeTy};
}

// ─── If statement ──────────────────────────────────────────────────────

void CodeGen::codegenIfStmt(IfStmt *stmt) {
  auto fn = builder_->GetInsertBlock()->getParent();

  auto condV = codegenExpr(stmt->condition.get());
  condV = toBool(condV);

  auto thenBB = llvm::BasicBlock::Create(*context_, "then", fn);
  auto elseBB = llvm::BasicBlock::Create(*context_, "else", fn);
  auto mergeBB = llvm::BasicBlock::Create(*context_, "ifcont", fn);

  builder_->CreateCondBr(condV, thenBB, elseBB);

  // Then
  builder_->SetInsertPoint(thenBB);
  codegenStmt(stmt->thenBranch.get());
  if (!builder_->GetInsertBlock()->getTerminator())
    builder_->CreateBr(mergeBB);

  // Else
  builder_->SetInsertPoint(elseBB);
  if (stmt->elseBranch) {
    codegenStmt(stmt->elseBranch.get());
  }
  if (!builder_->GetInsertBlock()->getTerminator())
    builder_->CreateBr(mergeBB);

  builder_->SetInsertPoint(mergeBB);
}

// ─── While statement ───────────────────────────────────────────────────

void CodeGen::codegenWhileStmt(WhileStmt *stmt) {
  auto fn = builder_->GetInsertBlock()->getParent();

  auto condBB = llvm::BasicBlock::Create(*context_, "while.cond", fn);
  auto bodyBB = llvm::BasicBlock::Create(*context_, "while.body", fn);
  auto endBB = llvm::BasicBlock::Create(*context_, "while.end", fn);

  breakTargets_.push_back(endBB);
  continueTargets_.push_back(condBB);

  builder_->CreateBr(condBB);

  // Condition
  builder_->SetInsertPoint(condBB);
  auto condV = codegenExpr(stmt->condition.get());
  condV = toBool(condV);
  builder_->CreateCondBr(condV, bodyBB, endBB);

  // Body
  builder_->SetInsertPoint(bodyBB);
  codegenStmt(stmt->body.get());
  if (!builder_->GetInsertBlock()->getTerminator())
    builder_->CreateBr(condBB);

  builder_->SetInsertPoint(endBB);

  breakTargets_.pop_back();
  continueTargets_.pop_back();
}

// ─── For statement ─────────────────────────────────────────────────────

void CodeGen::codegenForStmt(ForStmt *stmt) {
  auto fn = builder_->GetInsertBlock()->getParent();

  pushScope();

  // Init
  if (stmt->init)
    codegenStmt(stmt->init.get());

  auto condBB = llvm::BasicBlock::Create(*context_, "for.cond", fn);
  auto bodyBB = llvm::BasicBlock::Create(*context_, "for.body", fn);
  auto incrBB = llvm::BasicBlock::Create(*context_, "for.incr", fn);
  auto endBB = llvm::BasicBlock::Create(*context_, "for.end", fn);

  breakTargets_.push_back(endBB);
  continueTargets_.push_back(incrBB);

  builder_->CreateBr(condBB);

  // Condition
  builder_->SetInsertPoint(condBB);
  if (stmt->condition) {
    auto condV = codegenExpr(stmt->condition.get());
    condV = toBool(condV);
    builder_->CreateCondBr(condV, bodyBB, endBB);
  } else {
    builder_->CreateBr(bodyBB); // infinite loop if no condition
  }

  // Body
  builder_->SetInsertPoint(bodyBB);
  codegenStmt(stmt->body.get());
  if (!builder_->GetInsertBlock()->getTerminator())
    builder_->CreateBr(incrBB);

  // Increment
  builder_->SetInsertPoint(incrBB);
  if (stmt->increment)
    codegenExpr(stmt->increment.get());
  builder_->CreateBr(condBB);

  builder_->SetInsertPoint(endBB);

  breakTargets_.pop_back();
  continueTargets_.pop_back();
  popScope();
}

// ─── Do-while ─────────────────────────────────────────────────────────

void CodeGen::codegenDoWhileStmt(DoWhileStmt *stmt) {
  auto fn = builder_->GetInsertBlock()->getParent();

  auto bodyBB = llvm::BasicBlock::Create(*context_, "dowhile.body", fn);
  auto condBB = llvm::BasicBlock::Create(*context_, "dowhile.cond", fn);
  auto endBB = llvm::BasicBlock::Create(*context_, "dowhile.end", fn);

  breakTargets_.push_back(endBB);
  continueTargets_.push_back(condBB);

  builder_->CreateBr(bodyBB);

  // Body (executes at least once)
  builder_->SetInsertPoint(bodyBB);
  codegenStmt(stmt->body.get());
  if (!builder_->GetInsertBlock()->getTerminator())
    builder_->CreateBr(condBB);

  // Condition check at end
  builder_->SetInsertPoint(condBB);
  auto condV = codegenExpr(stmt->condition.get());
  condV = toBool(condV);
  builder_->CreateCondBr(condV, bodyBB, endBB);

  builder_->SetInsertPoint(endBB);

  breakTargets_.pop_back();
  continueTargets_.pop_back();
}

// ─── Switch ───────────────────────────────────────────────────────────

void CodeGen::codegenSwitchStmt(SwitchStmt *stmt) {
  auto fn = builder_->GetInsertBlock()->getParent();
  auto switchVal = codegenExpr(stmt->expr.get());

  auto endBB = llvm::BasicBlock::Create(*context_, "switch.end", fn);
  breakTargets_.push_back(endBB);

  // Find the default block, or use endBB
  llvm::BasicBlock *defaultBB = endBB;
  std::vector<std::pair<llvm::ConstantInt *, llvm::BasicBlock *>> caseBlocks;

  // Create basic blocks for each case
  for (size_t i = 0; i < stmt->cases.size(); i++) {
    auto &clause = stmt->cases[i];
    if (!clause.value) {
      // default case
      defaultBB = llvm::BasicBlock::Create(*context_, "switch.default", fn);
    } else {
      auto bb = llvm::BasicBlock::Create(*context_, "switch.case", fn);
      auto caseVal = codegenExpr(clause.value.get());
      auto ci = llvm::dyn_cast<llvm::ConstantInt>(caseVal);
      if (!ci)
        throw std::runtime_error(
            "switch case value must be a constant integer");
      caseBlocks.push_back({ci, bb});
    }
  }

  // Create the switch instruction
  auto switchInst =
      builder_->CreateSwitch(switchVal, defaultBB, caseBlocks.size());
  for (auto &[ci, bb] : caseBlocks) {
    switchInst->addCase(ci, bb);
  }

  // Generate code for each case
  size_t caseIdx = 0;
  for (size_t i = 0; i < stmt->cases.size(); i++) {
    auto &clause = stmt->cases[i];
    llvm::BasicBlock *bb;
    if (!clause.value) {
      bb = defaultBB;
    } else {
      bb = caseBlocks[caseIdx++].second;
    }
    builder_->SetInsertPoint(bb);
    for (auto &s : clause.body) {
      codegenStmt(s.get());
      if (builder_->GetInsertBlock()->getTerminator())
        break;
    }
    // Fall through to endBB if no break/terminator
    if (!builder_->GetInsertBlock()->getTerminator())
      builder_->CreateBr(endBB);
  }

  builder_->SetInsertPoint(endBB);
  breakTargets_.pop_back();
}

// ─── Global variable codegen ───────────────────────────────────────────

void CodeGen::codegenGlobalVar(VariableDecl *decl) {
  auto ty = getLLVMType(decl->type.get());
  llvm::Constant *initializer = nullptr;

  // Track pointee type for pointer globals
  llvm::Type *pointeeTy = nullptr;
  if (auto ptrType = dynamic_cast<PointerType *>(decl->type.get())) {
    pointeeTy = getLLVMType(ptrType->pointee.get());
  }

  // Determine initializer
  if (decl->init) {
    if (auto intLit = dynamic_cast<IntegerLiteral *>(decl->init.get())) {
      initializer = llvm::ConstantInt::get(ty, intLit->value, true);
    } else if (auto floatLit = dynamic_cast<FloatLiteral *>(decl->init.get())) {
      initializer = llvm::ConstantFP::get(ty, floatLit->value);
    } else if (auto boolLit = dynamic_cast<BoolLiteral *>(decl->init.get())) {
      initializer = llvm::ConstantInt::get(ty, boolLit->value ? 1 : 0);
    } else {
      // Default zero-initialize for non-constant initializers
      initializer = llvm::Constant::getNullValue(ty);
    }
  } else {
    initializer = llvm::Constant::getNullValue(ty);
  }

  auto gv = new llvm::GlobalVariable(*module_, ty, !decl->isMutable,
                                     llvm::GlobalValue::ExternalLinkage,
                                     initializer, decl->name);

  globals_[decl->name] = {gv, ty, decl->isMutable, pointeeTy};
}

// ─── Return ────────────────────────────────────────────────────────────

void CodeGen::codegenReturnStmt(ReturnStmt *stmt) {
  if (stmt->value) {
    auto val = codegenExpr(stmt->value.get());
    auto fn = builder_->GetInsertBlock()->getParent();
    auto retTy = fn->getReturnType();
    if (val->getType() != retTy) {
      if (retTy->isIntegerTy() && val->getType()->isIntegerTy())
        val = builder_->CreateIntCast(val, retTy, true);
      else if (retTy->isFloatingPointTy() && val->getType()->isIntegerTy())
        val = builder_->CreateSIToFP(val, retTy);
      else if (retTy->isIntegerTy() && val->getType()->isFloatingPointTy())
        val = builder_->CreateFPToSI(val, retTy);
    }
    builder_->CreateRet(val);
  } else {
    builder_->CreateRetVoid();
  }
}

// ─── Output ────────────────────────────────────────────────────────────

void CodeGen::codegenOutputStmt(OutputStmt *stmt) {
  auto val = codegenExpr(stmt->expr.get());
  auto printfFn = getOrDeclarePrintf();

  if (val->getType()->isIntegerTy(32)) {
    auto fmt = builder_->CreateGlobalStringPtr("%d\n", "fmt_int");
    builder_->CreateCall(printfFn, {fmt, val});
  } else if (val->getType()->isIntegerTy(64)) {
    auto fmt = builder_->CreateGlobalStringPtr("%lld\n", "fmt_i64");
    builder_->CreateCall(printfFn, {fmt, val});
  } else if (val->getType()->isIntegerTy(8)) {
    auto fmt = builder_->CreateGlobalStringPtr("%c\n", "fmt_char");
    builder_->CreateCall(printfFn, {fmt, val});
  } else if (val->getType()->isIntegerTy(1)) {
    // Bool: print "true" or "false"
    auto extended =
        builder_->CreateZExt(val, llvm::Type::getInt32Ty(*context_));
    auto fmt = builder_->CreateGlobalStringPtr("%d\n", "fmt_bool");
    builder_->CreateCall(printfFn, {fmt, extended});
  } else if (val->getType()->isDoubleTy()) {
    auto fmt = builder_->CreateGlobalStringPtr("%f\n", "fmt_f64");
    builder_->CreateCall(printfFn, {fmt, val});
  } else if (val->getType()->isFloatTy()) {
    auto extended =
        builder_->CreateFPExt(val, llvm::Type::getDoubleTy(*context_));
    auto fmt = builder_->CreateGlobalStringPtr("%f\n", "fmt_f32");
    builder_->CreateCall(printfFn, {fmt, extended});
  } else if (val->getType()->isPointerTy()) {
    // Assume it's a string (char*)
    auto fmt = builder_->CreateGlobalStringPtr("%s\n", "fmt_str");
    builder_->CreateCall(printfFn, {fmt, val});
  } else {
    // Fallback: try as i32
    auto fmt = builder_->CreateGlobalStringPtr("%d\n", "fmt_default");
    builder_->CreateCall(printfFn, {fmt, val});
  }
}

// ═══════════════════════════════════════════════════════════════════════
//  Expression codegen
// ═══════════════════════════════════════════════════════════════════════

llvm::Value *CodeGen::codegenExpr(Expression *expr) {
  if (auto intLit = dynamic_cast<IntegerLiteral *>(expr)) {
    return llvm::ConstantInt::get(*context_,
                                  llvm::APInt(32, intLit->value, true));
  }

  if (auto floatLit = dynamic_cast<FloatLiteral *>(expr)) {
    return llvm::ConstantFP::get(*context_, llvm::APFloat(floatLit->value));
  }

  if (auto strLit = dynamic_cast<StringLiteral *>(expr)) {
    return builder_->CreateGlobalStringPtr(strLit->value, "str");
  }

  if (auto charLit = dynamic_cast<CharLiteral *>(expr)) {
    return llvm::ConstantInt::get(*context_, llvm::APInt(8, charLit->value));
  }

  if (auto boolLit = dynamic_cast<BoolLiteral *>(expr)) {
    return llvm::ConstantInt::get(*context_,
                                  llvm::APInt(1, boolLit->value ? 1 : 0));
  }

  if (auto nullLit = dynamic_cast<NullLiteral *>(expr)) {
    return llvm::ConstantPointerNull::get(
        llvm::PointerType::getUnqual(*context_));
  }

  if (auto ident = dynamic_cast<IdentifierExpr *>(expr)) {
    auto var = lookupVar(ident->name);
    if (var) {
      return builder_->CreateLoad(var->type, var->alloca, ident->name);
    }
    // Check global variables
    auto git = globals_.find(ident->name);
    if (git != globals_.end()) {
      return builder_->CreateLoad(git->second.type, git->second.global,
                                  ident->name);
    }
    // Maybe it's a function name (for function pointers)
    auto fnIt = functions_.find(ident->name);
    if (fnIt != functions_.end())
      return fnIt->second;
    throw std::runtime_error("undeclared variable: " + ident->name);
  }

  if (auto bin = dynamic_cast<BinaryExpr *>(expr))
    return codegenBinary(bin);
  if (auto un = dynamic_cast<UnaryExpr *>(expr))
    return codegenUnary(un);
  if (auto call = dynamic_cast<CallExpr *>(expr))
    return codegenCall(call);
  if (auto assign = dynamic_cast<AssignmentExpr *>(expr))
    return codegenAssignment(assign);
  if (auto member = dynamic_cast<MemberAccessExpr *>(expr))
    return codegenMemberAccess(member);
  if (auto subscript = dynamic_cast<ArraySubscriptExpr *>(expr))
    return codegenArraySubscript(subscript);
  if (auto cast = dynamic_cast<CastExpr *>(expr))
    return codegenCast(cast);

  if (auto ternary = dynamic_cast<TernaryExpr *>(expr)) {
    auto condV = codegenExpr(ternary->condition.get());
    condV = toBool(condV);

    auto fn = builder_->GetInsertBlock()->getParent();
    auto thenBB = llvm::BasicBlock::Create(*context_, "tern.then", fn);
    auto elseBB = llvm::BasicBlock::Create(*context_, "tern.else", fn);
    auto mergeBB = llvm::BasicBlock::Create(*context_, "tern.merge", fn);

    builder_->CreateCondBr(condV, thenBB, elseBB);

    builder_->SetInsertPoint(thenBB);
    auto thenV = codegenExpr(ternary->thenExpr.get());
    builder_->CreateBr(mergeBB);
    thenBB = builder_->GetInsertBlock();

    builder_->SetInsertPoint(elseBB);
    auto elseV = codegenExpr(ternary->elseExpr.get());
    builder_->CreateBr(mergeBB);
    elseBB = builder_->GetInsertBlock();

    builder_->SetInsertPoint(mergeBB);
    auto phi = builder_->CreatePHI(thenV->getType(), 2, "tern");
    phi->addIncoming(thenV, thenBB);
    phi->addIncoming(elseV, elseBB);
    return phi;
  }

  if (auto arrLit = dynamic_cast<ArrayLiteralExpr *>(expr)) {
    if (arrLit->elements.empty())
      return llvm::ConstantPointerNull::get(
          llvm::PointerType::getUnqual(*context_));

    auto fn = builder_->GetInsertBlock()->getParent();
    auto firstVal = codegenExpr(arrLit->elements[0].get());
    auto elemTy = firstVal->getType();
    auto arrTy = llvm::ArrayType::get(elemTy, arrLit->elements.size());
    auto alloca = createEntryBlockAlloca(fn, "arr", arrTy);

    // Store first element
    auto gep0 = builder_->CreateConstGEP2_32(arrTy, alloca, 0, 0);
    builder_->CreateStore(firstVal, gep0);

    // Store remaining elements
    for (size_t i = 1; i < arrLit->elements.size(); i++) {
      auto val = codegenExpr(arrLit->elements[i].get());
      auto gep = builder_->CreateConstGEP2_32(arrTy, alloca, 0,
                                              static_cast<unsigned>(i));
      builder_->CreateStore(val, gep);
    }

    return builder_->CreateConstGEP2_32(arrTy, alloca, 0,
                                        0); // return pointer to first element
  }

  if (auto sizeOf = dynamic_cast<SizeofExpr *>(expr)) {
    auto ty = getLLVMType(sizeOf->type.get());
    auto dl = module_->getDataLayout();
    uint64_t sz = dl.getTypeAllocSize(ty);
    return llvm::ConstantInt::get(*context_, llvm::APInt(64, sz));
  }

  if (auto scopeRes = dynamic_cast<ScopeResolutionExpr *>(expr)) {
    // Enum variant
    auto eit = enumValues_.find(scopeRes->scope);
    if (eit != enumValues_.end()) {
      auto vit = eit->second.find(scopeRes->member);
      if (vit != eit->second.end()) {
        return llvm::ConstantInt::get(*context_,
                                      llvm::APInt(32, vit->second, true));
      }
    }
    throw std::runtime_error("unknown scope resolution: " + scopeRes->scope +
                             "::" + scopeRes->member);
  }

  if (auto newExpr = dynamic_cast<NewExpr *>(expr)) {
    auto ty = getLLVMType(newExpr->type.get());
    auto dl = module_->getDataLayout();
    uint64_t sz = dl.getTypeAllocSize(ty);
    auto mallocFn = getOrDeclareMalloc();
    auto sizeVal = llvm::ConstantInt::get(*context_, llvm::APInt(64, sz));
    auto rawPtr = builder_->CreateCall(mallocFn, {sizeVal}, "new_ptr");
    return rawPtr;
  }

  if (auto delExpr = dynamic_cast<DeleteExpr *>(expr)) {
    auto val = codegenExpr(delExpr->expr.get());
    auto freeFn = getOrDeclareFree();
    builder_->CreateCall(freeFn, {val});
    return llvm::ConstantInt::get(
        *context_,
        llvm::APInt(32, 0)); // delete returns void, but we need a value
  }

  // input() expression — reads an int from stdin via scanf
  if (auto inputExpr = dynamic_cast<InputExpr *>(expr)) {
    auto fn = builder_->GetInsertBlock()->getParent();
    auto intTy = llvm::Type::getInt32Ty(*context_);
    auto alloca = createEntryBlockAlloca(fn, "input_tmp", intTy);
    auto scanfFn = getOrDeclareScanf();
    auto fmt = builder_->CreateGlobalStringPtr("%d", "fmt_input");
    builder_->CreateCall(scanfFn, {fmt, alloca});
    return builder_->CreateLoad(intTy, alloca, "input_val");
  }

  throw std::runtime_error("cannot codegen expression");
}

// ─── Binary expressions ───────────────────────────────────────────────

llvm::Value *CodeGen::codegenBinary(BinaryExpr *expr) {
  auto lhs = codegenExpr(expr->left.get());
  auto rhs = codegenExpr(expr->right.get());

  // ── Pointer arithmetic ──────────────────────────────────────────────
  bool lhsIsPtr = lhs->getType()->isPointerTy();
  bool rhsIsPtr = rhs->getType()->isPointerTy();

  if (lhsIsPtr || rhsIsPtr) {
    // ptr + int or int + ptr → GEP
    if (expr->op == TokenType::PLUS) {
      if (lhsIsPtr && rhs->getType()->isIntegerTy()) {
        return builder_->CreateGEP(llvm::Type::getInt8Ty(*context_), lhs, rhs, "ptr_add");
      }
      if (rhsIsPtr && lhs->getType()->isIntegerTy()) {
        return builder_->CreateGEP(llvm::Type::getInt8Ty(*context_), rhs, lhs, "ptr_add");
      }
    }
    // ptr - int → GEP with negative offset
    if (expr->op == TokenType::MINUS) {
      if (lhsIsPtr && rhs->getType()->isIntegerTy()) {
        auto neg = builder_->CreateNeg(rhs, "neg");
        return builder_->CreateGEP(llvm::Type::getInt8Ty(*context_), lhs, neg, "ptr_sub");
      }
      // ptr - ptr → difference
      if (lhsIsPtr && rhsIsPtr) {
        auto i64Ty = llvm::Type::getInt64Ty(*context_);
        auto lhsInt = builder_->CreatePtrToInt(lhs, i64Ty);
        auto rhsInt = builder_->CreatePtrToInt(rhs, i64Ty);
        return builder_->CreateSub(lhsInt, rhsInt, "ptr_diff");
      }
    }
    // ptr == ptr, ptr != ptr, ptr == null, null == ptr
    if (expr->op == TokenType::EQUAL_EQUAL || expr->op == TokenType::EXCLAIM_EQUAL) {
      if (lhsIsPtr && rhsIsPtr) {
        if (expr->op == TokenType::EQUAL_EQUAL)
          return builder_->CreateICmpEQ(lhs, rhs, "ptr_eq");
        else
          return builder_->CreateICmpNE(lhs, rhs, "ptr_ne");
      }
    }
  }

  // ── Standard numeric promotion ──────────────────────────────────────
  // Promote types if needed
  bool isFloat = lhs->getType()->isFloatingPointTy() ||
                 rhs->getType()->isFloatingPointTy();
  if (isFloat) {
    auto doubleTy = llvm::Type::getDoubleTy(*context_);
    if (!lhs->getType()->isFloatingPointTy())
      lhs = builder_->CreateSIToFP(lhs, doubleTy);
    else if (lhs->getType()->isFloatTy())
      lhs = builder_->CreateFPExt(lhs, doubleTy);
    if (!rhs->getType()->isFloatingPointTy())
      rhs = builder_->CreateSIToFP(rhs, doubleTy);
    else if (rhs->getType()->isFloatTy())
      rhs = builder_->CreateFPExt(rhs, doubleTy);
  } else {
    // Integer promotion: match sizes
    if (lhs->getType() != rhs->getType()) {
      if (lhs->getType()->isIntegerTy() && rhs->getType()->isIntegerTy()) {
        auto lhsBits = lhs->getType()->getIntegerBitWidth();
        auto rhsBits = rhs->getType()->getIntegerBitWidth();
        if (lhsBits > rhsBits)
          rhs = builder_->CreateSExt(rhs, lhs->getType());
        else
          lhs = builder_->CreateSExt(lhs, rhs->getType());
      }
    }
  }

  switch (expr->op) {
  case TokenType::PLUS:
    return isFloat ? builder_->CreateFAdd(lhs, rhs, "add")
                   : builder_->CreateAdd(lhs, rhs, "add");
  case TokenType::MINUS:
    return isFloat ? builder_->CreateFSub(lhs, rhs, "sub")
                   : builder_->CreateSub(lhs, rhs, "sub");
  case TokenType::STAR:
    return isFloat ? builder_->CreateFMul(lhs, rhs, "mul")
                   : builder_->CreateMul(lhs, rhs, "mul");
  case TokenType::SLASH:
    return isFloat ? builder_->CreateFDiv(lhs, rhs, "div")
                   : builder_->CreateSDiv(lhs, rhs, "div");
  case TokenType::PERCENT:
    return isFloat ? builder_->CreateFRem(lhs, rhs, "mod")
                   : builder_->CreateSRem(lhs, rhs, "mod");

  case TokenType::EQUAL_EQUAL:
    return isFloat ? builder_->CreateFCmpOEQ(lhs, rhs, "eq")
                   : builder_->CreateICmpEQ(lhs, rhs, "eq");
  case TokenType::EXCLAIM_EQUAL:
    return isFloat ? builder_->CreateFCmpONE(lhs, rhs, "ne")
                   : builder_->CreateICmpNE(lhs, rhs, "ne");
  case TokenType::LESS:
    return isFloat ? builder_->CreateFCmpOLT(lhs, rhs, "lt")
                   : builder_->CreateICmpSLT(lhs, rhs, "lt");
  case TokenType::GREATER:
    return isFloat ? builder_->CreateFCmpOGT(lhs, rhs, "gt")
                   : builder_->CreateICmpSGT(lhs, rhs, "gt");
  case TokenType::LESS_EQUAL:
    return isFloat ? builder_->CreateFCmpOLE(lhs, rhs, "le")
                   : builder_->CreateICmpSLE(lhs, rhs, "le");
  case TokenType::GREATER_EQUAL:
    return isFloat ? builder_->CreateFCmpOGE(lhs, rhs, "ge")
                   : builder_->CreateICmpSGE(lhs, rhs, "ge");

  case TokenType::AMPERSAND_AMPERSAND: {
    auto lhsBool = toBool(lhs);
    auto rhsBool = toBool(rhs);
    return builder_->CreateAnd(lhsBool, rhsBool, "and");
  }
  case TokenType::PIPE_PIPE: {
    auto lhsBool = toBool(lhs);
    auto rhsBool = toBool(rhs);
    return builder_->CreateOr(lhsBool, rhsBool, "or");
  }

  case TokenType::AMPERSAND:
    return builder_->CreateAnd(lhs, rhs, "band");
  case TokenType::PIPE:
    return builder_->CreateOr(lhs, rhs, "bor");
  case TokenType::CARET:
    return builder_->CreateXor(lhs, rhs, "bxor");
  case TokenType::SHL:
    return builder_->CreateShl(lhs, rhs, "shl");
  case TokenType::SHR:
    return builder_->CreateAShr(lhs, rhs, "shr");

  default:
    throw std::runtime_error("unsupported binary operator");
  }
}

// ─── Unary expressions ────────────────────────────────────────────────

llvm::Value *CodeGen::codegenUnary(UnaryExpr *expr) {
  if (expr->prefix) {
    switch (expr->op) {
    case TokenType::MINUS: {
      auto val = codegenExpr(expr->operand.get());
      if (val->getType()->isFloatingPointTy())
        return builder_->CreateFNeg(val, "neg");
      return builder_->CreateNeg(val, "neg");
    }
    case TokenType::EXCLAIM: {
      auto val = codegenExpr(expr->operand.get());
      val = toBool(val);
      return builder_->CreateNot(val, "not");
    }
    case TokenType::TILDE: {
      auto val = codegenExpr(expr->operand.get());
      return builder_->CreateNot(val, "bnot");
    }
    case TokenType::AMPERSAND: {
      // Address-of — supports variables, globals, struct fields, array elements
      if (auto ident = dynamic_cast<IdentifierExpr *>(expr->operand.get())) {
        auto var = lookupVar(ident->name);
        if (var)
          return var->alloca;
        auto git = globals_.find(ident->name);
        if (git != globals_.end())
          return git->second.global;
        throw std::runtime_error("cannot take address of " + ident->name);
      }
      // &obj.field
      if (auto member = dynamic_cast<MemberAccessExpr *>(expr->operand.get())) {
        llvm::Type *fieldTy = nullptr;
        return codegenLValue(member, &fieldTy);
      }
      // &arr[i]
      if (auto sub = dynamic_cast<ArraySubscriptExpr *>(expr->operand.get())) {
        llvm::Type *elemTy = nullptr;
        return codegenLValue(sub, &elemTy);
      }
      throw std::runtime_error("cannot take address of expression");
    }
    case TokenType::STAR: {
      // Dereference — type-aware via pointeeType tracking
      auto operand = expr->operand.get();
      // Check if operand is an identifier with known pointee type
      if (auto ident = dynamic_cast<IdentifierExpr *>(operand)) {
        auto var = lookupVar(ident->name);
        if (var) {
          auto ptrVal = builder_->CreateLoad(var->type, var->alloca, ident->name);
          llvm::Type *derefTy = var->pointeeType
                                    ? var->pointeeType
                                    : llvm::Type::getInt32Ty(*context_);
          return builder_->CreateLoad(derefTy, ptrVal, "deref");
        }
        auto git = globals_.find(ident->name);
        if (git != globals_.end()) {
          auto ptrVal = builder_->CreateLoad(git->second.type, git->second.global, ident->name);
          llvm::Type *derefTy = git->second.pointeeType
                                    ? git->second.pointeeType
                                    : llvm::Type::getInt32Ty(*context_);
          return builder_->CreateLoad(derefTy, ptrVal, "deref");
        }
      }
      // Fallback: evaluate and dereference as i32
      auto val = codegenExpr(operand);
      return builder_->CreateLoad(llvm::Type::getInt32Ty(*context_), val,
                                  "deref");
    }
    case TokenType::PLUS_PLUS: {
      // Pre-increment
      if (auto ident = dynamic_cast<IdentifierExpr *>(expr->operand.get())) {
        auto var = lookupVar(ident->name);
        if (!var)
          throw std::runtime_error("undeclared: " + ident->name);
        auto val = builder_->CreateLoad(var->type, var->alloca, ident->name);
        auto one = llvm::ConstantInt::get(var->type, 1);
        auto inc = builder_->CreateAdd(val, one, "inc");
        builder_->CreateStore(inc, var->alloca);
        return inc;
      }
      break;
    }
    case TokenType::MINUS_MINUS: {
      if (auto ident = dynamic_cast<IdentifierExpr *>(expr->operand.get())) {
        auto var = lookupVar(ident->name);
        if (!var)
          throw std::runtime_error("undeclared: " + ident->name);
        auto val = builder_->CreateLoad(var->type, var->alloca, ident->name);
        auto one = llvm::ConstantInt::get(var->type, 1);
        auto dec = builder_->CreateSub(val, one, "dec");
        builder_->CreateStore(dec, var->alloca);
        return dec;
      }
      break;
    }
    default:
      break;
    }
  } else {
    // Postfix ++ and --
    if (auto ident = dynamic_cast<IdentifierExpr *>(expr->operand.get())) {
      auto var = lookupVar(ident->name);
      if (!var)
        throw std::runtime_error("undeclared: " + ident->name);
      auto val = builder_->CreateLoad(var->type, var->alloca, ident->name);
      auto one = llvm::ConstantInt::get(var->type, 1);
      llvm::Value *newVal;
      if (expr->op == TokenType::PLUS_PLUS)
        newVal = builder_->CreateAdd(val, one, "inc");
      else
        newVal = builder_->CreateSub(val, one, "dec");
      builder_->CreateStore(newVal, var->alloca);
      return val; // return original value (postfix)
    }
  }

  throw std::runtime_error("unsupported unary expression");
}

// ─── Function call ─────────────────────────────────────────────────────

llvm::Value *CodeGen::codegenCall(CallExpr *expr) {
  // Check for method call: obj.method(args)
  if (auto memberAccess = dynamic_cast<MemberAccessExpr *>(expr->callee.get())) {
    // Get the object
    auto objIdent = dynamic_cast<IdentifierExpr *>(memberAccess->object.get());
    if (objIdent) {
      auto var = lookupVar(objIdent->name);
      if (var) {
        // Determine struct type name
        std::string structName;
        if (auto st = llvm::dyn_cast<llvm::StructType>(var->type)) {
          structName = st->getName().str();
        } else if (var->type->isPointerTy() && var->pointeeType) {
          if (auto st = llvm::dyn_cast<llvm::StructType>(var->pointeeType)) {
            structName = st->getName().str();
          }
        }
        // Check if this struct has the method
        auto structIt = methodTable_.find(structName);
        if (structIt != methodTable_.end()) {
          auto methodIt = structIt->second.find(memberAccess->member);
          if (methodIt != structIt->second.end()) {
            std::string mangledName = methodIt->second;
            auto callee = module_->getFunction(mangledName);
            if (!callee)
              throw std::runtime_error("unknown method: " + mangledName);

            // Build args: &obj (self) + provided args
            std::vector<llvm::Value *> args;
            if (var->type->isPointerTy()) {
              // Already a pointer (e.g. self inside another method)
              auto ptrVal = builder_->CreateLoad(var->type, var->alloca, objIdent->name);
              args.push_back(ptrVal);
            } else {
              // Take address of the struct
              args.push_back(var->alloca);
            }
            for (auto &a : expr->args) {
              args.push_back(codegenExpr(a.get()));
            }

            if (callee->getReturnType()->isVoidTy()) {
              builder_->CreateCall(callee, args);
              return llvm::ConstantInt::get(*context_, llvm::APInt(32, 0));
            }
            return builder_->CreateCall(callee, args, "method_call");
          }
        }
      }
    }
  }

  // Get the function name
  std::string fnName;
  if (auto ident = dynamic_cast<IdentifierExpr *>(expr->callee.get())) {
    fnName = ident->name;
  } else {
    throw std::runtime_error("unsupported callee expression");
  }

  auto callee = module_->getFunction(fnName);
  if (!callee)
    throw std::runtime_error("unknown function: " + fnName);

  // Build argument list, filling in defaults for missing args
  std::vector<llvm::Value *> args;
  size_t numProvided = expr->args.size();
  size_t numParams = callee->arg_size();

  for (size_t i = 0; i < numParams; i++) {
    llvm::Value *val;
    if (i < numProvided) {
      val = codegenExpr(expr->args[i].get());
    } else {
      // Look up default value from funcDecls_
      auto declIt = funcDecls_.find(fnName);
      if (declIt != funcDecls_.end() && i < declIt->second->params.size() &&
          declIt->second->params[i].defaultValue) {
        val = codegenExpr(declIt->second->params[i].defaultValue.get());
      } else {
        throw std::runtime_error("not enough arguments for function '" + fnName + "'");
      }
    }
    // Coerce argument types
    auto paramTy = callee->getArg(i)->getType();
    if (val->getType() != paramTy) {
      if (paramTy->isIntegerTy() && val->getType()->isIntegerTy()) {
        val = builder_->CreateIntCast(val, paramTy, true);
      } else if (paramTy->isFloatingPointTy() &&
                 val->getType()->isIntegerTy()) {
        val = builder_->CreateSIToFP(val, paramTy);
      } else if (paramTy->isIntegerTy() &&
                 val->getType()->isFloatingPointTy()) {
        val = builder_->CreateFPToSI(val, paramTy);
      }
    }
    args.push_back(val);
  }

  if (callee->getReturnType()->isVoidTy()) {
    builder_->CreateCall(callee, args);
    return llvm::ConstantInt::get(
        *context_, llvm::APInt(32, 0)); // void funcs return 0 as value
  }

  return builder_->CreateCall(callee, args, "call");
}

// ─── Assignment ────────────────────────────────────────────────────────

llvm::Value *CodeGen::codegenAssignment(AssignmentExpr *expr) {
  auto val = codegenExpr(expr->value.get());

  // Simple variable assignment
  if (auto ident = dynamic_cast<IdentifierExpr *>(expr->target.get())) {
    auto var = lookupVar(ident->name);
    if (!var) {
      // Check globals
      auto git = globals_.find(ident->name);
      if (git != globals_.end()) {
        if (expr->op != TokenType::EQUAL) {
          auto curVal = builder_->CreateLoad(git->second.type, git->second.global, ident->name);
          switch (expr->op) {
          case TokenType::PLUS_EQUAL: val = builder_->CreateAdd(curVal, val); break;
          case TokenType::MINUS_EQUAL: val = builder_->CreateSub(curVal, val); break;
          case TokenType::STAR_EQUAL: val = builder_->CreateMul(curVal, val); break;
          case TokenType::SLASH_EQUAL: val = builder_->CreateSDiv(curVal, val); break;
          case TokenType::PERCENT_EQUAL: val = builder_->CreateSRem(curVal, val); break;
          default: throw std::runtime_error("unsupported compound assignment");
          }
        }
        if (val->getType() != git->second.type) {
          if (git->second.type->isIntegerTy() && val->getType()->isIntegerTy())
            val = builder_->CreateIntCast(val, git->second.type, true);
        }
        builder_->CreateStore(val, git->second.global);
        return val;
      }
      throw std::runtime_error("undeclared variable: " + ident->name);
    }

    if (expr->op != TokenType::EQUAL) {
      auto curVal = builder_->CreateLoad(var->type, var->alloca, ident->name);
      // Compound assignment
      switch (expr->op) {
      case TokenType::PLUS_EQUAL:
        val = curVal->getType()->isFloatingPointTy()
                  ? builder_->CreateFAdd(curVal, val)
                  : builder_->CreateAdd(curVal, val);
        break;
      case TokenType::MINUS_EQUAL:
        val = curVal->getType()->isFloatingPointTy()
                  ? builder_->CreateFSub(curVal, val)
                  : builder_->CreateSub(curVal, val);
        break;
      case TokenType::STAR_EQUAL:
        val = curVal->getType()->isFloatingPointTy()
                  ? builder_->CreateFMul(curVal, val)
                  : builder_->CreateMul(curVal, val);
        break;
      case TokenType::SLASH_EQUAL:
        val = curVal->getType()->isFloatingPointTy()
                  ? builder_->CreateFDiv(curVal, val)
                  : builder_->CreateSDiv(curVal, val);
        break;
      case TokenType::PERCENT_EQUAL:
        val = builder_->CreateSRem(curVal, val);
        break;
      default:
        throw std::runtime_error("unsupported compound assignment");
      }
    }

    // Type coercion for assignment
    if (val->getType() != var->type) {
      if (var->type->isIntegerTy() && val->getType()->isIntegerTy())
        val = builder_->CreateIntCast(val, var->type, true);
      else if (var->type->isFloatingPointTy() && val->getType()->isIntegerTy())
        val = builder_->CreateSIToFP(val, var->type);
      else if (var->type->isIntegerTy() && val->getType()->isFloatingPointTy())
        val = builder_->CreateFPToSI(val, var->type);
    }

    builder_->CreateStore(val, var->alloca);
    return val;
  }

  // Dereference assignment: *ptr = val
  if (auto deref = dynamic_cast<UnaryExpr *>(expr->target.get())) {
    if (deref->prefix && deref->op == TokenType::STAR) {
      llvm::Type *targetTy = nullptr;
      // Get the pointer value
      if (auto ident = dynamic_cast<IdentifierExpr *>(deref->operand.get())) {
        auto var = lookupVar(ident->name);
        if (var) {
          auto ptrVal = builder_->CreateLoad(var->type, var->alloca, ident->name);
          targetTy = var->pointeeType ? var->pointeeType : llvm::Type::getInt32Ty(*context_);
          if (val->getType() != targetTy) {
            if (targetTy->isIntegerTy() && val->getType()->isIntegerTy())
              val = builder_->CreateIntCast(val, targetTy, true);
            else if (targetTy->isFloatingPointTy() && val->getType()->isIntegerTy())
              val = builder_->CreateSIToFP(val, targetTy);
          }
          builder_->CreateStore(val, ptrVal);
          return val;
        }
      }
      // Fallback: evaluate pointer expression
      auto ptrVal = codegenExpr(deref->operand.get());
      builder_->CreateStore(val, ptrVal);
      return val;
    }
  }

  // Member access assignment: obj.field = val
  if (auto member = dynamic_cast<MemberAccessExpr *>(expr->target.get())) {
    llvm::Type *fieldTy = nullptr;
    auto addr = codegenLValue(member, &fieldTy);
    if (fieldTy && val->getType() != fieldTy) {
      if (fieldTy->isIntegerTy() && val->getType()->isIntegerTy())
        val = builder_->CreateIntCast(val, fieldTy, true);
      else if (fieldTy->isFloatingPointTy() && val->getType()->isIntegerTy())
        val = builder_->CreateSIToFP(val, fieldTy);
    }
    builder_->CreateStore(val, addr);
    return val;
  }

  // Array subscript assignment: arr[i] = val
  if (auto sub = dynamic_cast<ArraySubscriptExpr *>(expr->target.get())) {
    if (auto arrIdent = dynamic_cast<IdentifierExpr *>(sub->array.get())) {
      auto var = lookupVar(arrIdent->name);
      if (!var)
        throw std::runtime_error("undeclared: " + arrIdent->name);

      auto idx = codegenExpr(sub->index.get());
      if (var->type->isArrayTy()) {
        auto zero = llvm::ConstantInt::get(*context_, llvm::APInt(32, 0));
        auto gep = builder_->CreateGEP(var->type, var->alloca, {zero, idx},
                                       "arr_elem");
        builder_->CreateStore(val, gep);
      } else if (var->type->isPointerTy()) {
        // Pointer subscript — use pointee type if known
        auto ptr = builder_->CreateLoad(var->type, var->alloca, arrIdent->name);
        llvm::Type *elemTy = var->pointeeType ? var->pointeeType : llvm::Type::getInt32Ty(*context_);
        auto gep = builder_->CreateGEP(elemTy, ptr, idx, "ptr_elem");
        if (val->getType() != elemTy) {
          if (elemTy->isIntegerTy() && val->getType()->isIntegerTy())
            val = builder_->CreateIntCast(val, elemTy, true);
        }
        builder_->CreateStore(val, gep);
      }
      return val;
    }
  }

  throw std::runtime_error("unsupported assignment target");
}

// ─── Member access ─────────────────────────────────────────────────────

llvm::Value *CodeGen::codegenMemberAccess(MemberAccessExpr *expr) {
  if (auto objIdent = dynamic_cast<IdentifierExpr *>(expr->object.get())) {
    auto var = lookupVar(objIdent->name);
    if (!var)
      throw std::runtime_error("undeclared: " + objIdent->name);

    llvm::StructType *structTy = nullptr;
    llvm::Value *basePtr = nullptr;

    // Handle pointer-to-struct (e.g. self inside method bodies)
    if (var->type->isPointerTy() && var->pointeeType) {
      structTy = llvm::dyn_cast<llvm::StructType>(var->pointeeType);
      if (structTy)
        basePtr = builder_->CreateLoad(var->type, var->alloca, objIdent->name);
    }
    // Fall back to direct struct variable
    if (!structTy) {
      structTy = llvm::dyn_cast<llvm::StructType>(var->type);
      if (!structTy)
        throw std::runtime_error("not a struct: " + objIdent->name);
      basePtr = var->alloca;
    }

    auto nameIt = structFieldNames_.find(structTy->getName().str());
    if (nameIt == structFieldNames_.end())
      throw std::runtime_error("unknown struct type");

    int fieldIdx = -1;
    for (size_t i = 0; i < nameIt->second.size(); i++) {
      if (nameIt->second[i] == expr->member) {
        fieldIdx = i;
        break;
      }
    }
    if (fieldIdx < 0)
      throw std::runtime_error("unknown field: " + expr->member);

    auto gep = builder_->CreateStructGEP(structTy, basePtr, fieldIdx,
                                         expr->member);
    auto fieldTy = structTy->getElementType(fieldIdx);
    return builder_->CreateLoad(fieldTy, gep, expr->member + "_val");
  }
  throw std::runtime_error("complex member access not yet supported");
}

// ─── Array subscript ───────────────────────────────────────────────────

llvm::Value *CodeGen::codegenArraySubscript(ArraySubscriptExpr *expr) {
  if (auto arrIdent = dynamic_cast<IdentifierExpr *>(expr->array.get())) {
    auto var = lookupVar(arrIdent->name);
    if (!var)
      throw std::runtime_error("undeclared: " + arrIdent->name);

    auto idx = codegenExpr(expr->index.get());

    if (var->type->isArrayTy()) {
      auto zero = llvm::ConstantInt::get(*context_, llvm::APInt(32, 0));
      auto gep =
          builder_->CreateGEP(var->type, var->alloca, {zero, idx}, "arr_elem");
      auto elemTy = var->type->getArrayElementType();
      return builder_->CreateLoad(elemTy, gep, "arr_val");
    } else if (var->type->isPointerTy()) {
      auto ptr = builder_->CreateLoad(var->type, var->alloca, arrIdent->name);
      // Use tracked pointee type for type-aware subscript
      llvm::Type *elemTy = var->pointeeType ? var->pointeeType : llvm::Type::getInt32Ty(*context_);
      auto gep = builder_->CreateGEP(elemTy, ptr, idx, "ptr_elem");
      return builder_->CreateLoad(elemTy, gep, "ptr_val");
    }
  }
  throw std::runtime_error("complex array subscript not yet supported");
}

// ─── Cast ──────────────────────────────────────────────────────────────

llvm::Value *CodeGen::codegenCast(CastExpr *expr) {
  auto val = codegenExpr(expr->expr.get());
  auto targetTy = getLLVMType(expr->targetType.get());

  if (val->getType() == targetTy)
    return val;

  // Int to int
  if (val->getType()->isIntegerTy() && targetTy->isIntegerTy())
    return builder_->CreateIntCast(val, targetTy, true);

  // Int to float
  if (val->getType()->isIntegerTy() && targetTy->isFloatingPointTy())
    return builder_->CreateSIToFP(val, targetTy);

  // Float to int
  if (val->getType()->isFloatingPointTy() && targetTy->isIntegerTy())
    return builder_->CreateFPToSI(val, targetTy);

  // Float to float
  if (val->getType()->isFloatingPointTy() && targetTy->isFloatingPointTy()) {
    if (targetTy->getPrimitiveSizeInBits() >
        val->getType()->getPrimitiveSizeInBits())
      return builder_->CreateFPExt(val, targetTy);
    else
      return builder_->CreateFPTrunc(val, targetTy);
  }

  // Pointer casts
  if (val->getType()->isPointerTy() && targetTy->isPointerTy())
    return val; // opaque pointers in modern LLVM

  throw std::runtime_error("unsupported cast");
}

// ═══════════════════════════════════════════════════════════════════════
//  LValue resolution — returns an address for assignment targets
// ═══════════════════════════════════════════════════════════════════════

llvm::Value *CodeGen::codegenLValue(Expression *expr, llvm::Type **outType) {
  // Identifier → alloca or global address
  if (auto ident = dynamic_cast<IdentifierExpr *>(expr)) {
    auto var = lookupVar(ident->name);
    if (var) {
      if (outType) *outType = var->type;
      return var->alloca;
    }
    auto git = globals_.find(ident->name);
    if (git != globals_.end()) {
      if (outType) *outType = git->second.type;
      return git->second.global;
    }
    throw std::runtime_error("undeclared: " + ident->name);
  }

  // Member access: obj.field → GEP to field
  if (auto member = dynamic_cast<MemberAccessExpr *>(expr)) {
    if (auto objIdent = dynamic_cast<IdentifierExpr *>(member->object.get())) {
      auto var = lookupVar(objIdent->name);
      if (!var)
        throw std::runtime_error("undeclared: " + objIdent->name);

      llvm::StructType *structTy = nullptr;
      llvm::Value *basePtr = nullptr;

      // Check if object is a pointer-to-struct (e.g. 'self' inside methods)
      if (var->type->isPointerTy() && var->pointeeType) {
        structTy = llvm::dyn_cast<llvm::StructType>(var->pointeeType);
        if (structTy)
          basePtr = builder_->CreateLoad(var->type, var->alloca, objIdent->name);
      }
      // Otherwise try direct struct type
      if (!structTy) {
        structTy = llvm::dyn_cast<llvm::StructType>(var->type);
        if (!structTy)
          throw std::runtime_error("not a struct: " + objIdent->name);
        basePtr = var->alloca;
      }

      auto nameIt = structFieldNames_.find(structTy->getName().str());
      if (nameIt == structFieldNames_.end())
        throw std::runtime_error("unknown struct type");

      int fieldIdx = -1;
      for (size_t i = 0; i < nameIt->second.size(); i++) {
        if (nameIt->second[i] == member->member) {
          fieldIdx = i;
          break;
        }
      }
      if (fieldIdx < 0)
        throw std::runtime_error("unknown field: " + member->member);

      if (outType) *outType = structTy->getElementType(fieldIdx);
      return builder_->CreateStructGEP(structTy, basePtr, fieldIdx,
                                       member->member);
    }
  }

  // Array subscript: arr[i] → GEP
  if (auto sub = dynamic_cast<ArraySubscriptExpr *>(expr)) {
    if (auto arrIdent = dynamic_cast<IdentifierExpr *>(sub->array.get())) {
      auto var = lookupVar(arrIdent->name);
      if (!var)
        throw std::runtime_error("undeclared: " + arrIdent->name);

      auto idx = codegenExpr(sub->index.get());
      if (var->type->isArrayTy()) {
        auto zero = llvm::ConstantInt::get(*context_, llvm::APInt(32, 0));
        if (outType) *outType = var->type->getArrayElementType();
        return builder_->CreateGEP(var->type, var->alloca, {zero, idx},
                                   "arr_elem");
      } else if (var->type->isPointerTy()) {
        auto ptr = builder_->CreateLoad(var->type, var->alloca, arrIdent->name);
        llvm::Type *elemTy = var->pointeeType ? var->pointeeType : llvm::Type::getInt32Ty(*context_);
        if (outType) *outType = elemTy;
        return builder_->CreateGEP(elemTy, ptr, idx, "ptr_elem");
      }
    }
  }

  // Dereference: *ptr → load pointer then return address
  if (auto deref = dynamic_cast<UnaryExpr *>(expr)) {
    if (deref->prefix && deref->op == TokenType::STAR) {
      if (auto ident = dynamic_cast<IdentifierExpr *>(deref->operand.get())) {
        auto var = lookupVar(ident->name);
        if (var) {
          auto ptrVal = builder_->CreateLoad(var->type, var->alloca, ident->name);
          if (outType) *outType = var->pointeeType ? var->pointeeType : llvm::Type::getInt32Ty(*context_);
          return ptrVal;
        }
      }
      auto ptrVal = codegenExpr(deref->operand.get());
      if (outType) *outType = llvm::Type::getInt32Ty(*context_);
      return ptrVal;
    }
  }

  throw std::runtime_error("cannot compute lvalue of expression");
}

// ═══════════════════════════════════════════════════════════════════════
//  Helpers
// ═══════════════════════════════════════════════════════════════════════

llvm::Value *CodeGen::toBool(llvm::Value *val) {
  if (val->getType()->isIntegerTy(1))
    return val;
  if (val->getType()->isIntegerTy())
    return builder_->CreateICmpNE(
        val, llvm::ConstantInt::get(val->getType(), 0), "tobool");
  if (val->getType()->isFloatingPointTy())
    return builder_->CreateFCmpONE(
        val, llvm::ConstantFP::get(val->getType(), 0.0), "tobool");
  if (val->getType()->isPointerTy())
    return builder_->CreateICmpNE(
        builder_->CreatePtrToInt(val, llvm::Type::getInt64Ty(*context_)),
        llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context_), 0), "tobool");
  return val;
}

// ═══════════════════════════════════════════════════════════════════════
//  IR output
// ═══════════════════════════════════════════════════════════════════════

void CodeGen::dumpIR() const { module_->print(llvm::outs(), nullptr); }

std::string CodeGen::getIRString() const {
  std::string ir;
  llvm::raw_string_ostream os(ir);
  module_->print(os, nullptr);
  return ir;
}

bool CodeGen::emitObjectFile(const std::string &filename) {
  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmParser();
  llvm::InitializeNativeTargetAsmPrinter();

  auto targetTriple = llvm::Triple(llvm::sys::getDefaultTargetTriple());
  module_->setTargetTriple(targetTriple);

  std::string error;
  auto target = llvm::TargetRegistry::lookupTarget(targetTriple.str(), error);
  if (!target) {
    std::cerr << "Error: " << error << "\n";
    return false;
  }

  auto cpu = "generic";
  auto features = "";
  llvm::TargetOptions opt;
  auto targetMachine = target->createTargetMachine(targetTriple, cpu, features,
                                                   opt, llvm::Reloc::PIC_);

  module_->setDataLayout(targetMachine->createDataLayout());

  std::error_code ec;
  llvm::raw_fd_ostream dest(filename, ec, llvm::sys::fs::OF_None);
  if (ec) {
    std::cerr << "Could not open file: " << ec.message() << "\n";
    return false;
  }

  llvm::legacy::PassManager pm;
  if (targetMachine->addPassesToEmitFile(pm, dest, nullptr,
                                         llvm::CodeGenFileType::ObjectFile)) {
    std::cerr << "Target machine can't emit object file\n";
    return false;
  }

  pm.run(*module_);
  dest.flush();
  return true;
}

#pragma clang diagnostic pop

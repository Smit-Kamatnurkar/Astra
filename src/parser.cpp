#include "parser.hpp"
#include <sstream>
#include <stdexcept>

// ═══════════════════════════════════════════════════════════════════════
//  Construction / helpers
// ═══════════════════════════════════════════════════════════════════════

Parser::Parser(const std::vector<Token> &tokens, const std::string &filename)
    : tokens_(tokens), filename_(filename) {}

const Token &Parser::peek() const { return tokens_[pos_]; }

const Token &Parser::peekAt(size_t offset) const {
  size_t i = pos_ + offset;
  if (i >= tokens_.size())
    return tokens_.back();
  return tokens_[i];
}

const Token &Parser::advance() { return tokens_[pos_++]; }

bool Parser::check(TokenType ty) const { return peek().type == ty; }

bool Parser::checkKw(const std::string &kw) const {
  return peek().type == TokenType::KEYWORD && peek().value == kw;
}

bool Parser::match(TokenType ty) {
  if (check(ty)) {
    advance();
    return true;
  }
  return false;
}

bool Parser::matchKw(const std::string &kw) {
  if (checkKw(kw)) {
    advance();
    return true;
  }
  return false;
}

const Token &Parser::expect(TokenType ty, const std::string &msg) {
  if (!check(ty))
    error(msg + " (got '" + peek().value + "')");
  return advance();
}

void Parser::expectKw(const std::string &kw, const std::string &msg) {
  if (!checkKw(kw))
    error(msg + " (got '" + peek().value + "')");
  advance();
}

[[noreturn]] void Parser::error(const std::string &msg) const {
  auto &loc = peek().loc;
  std::ostringstream os;
  os << filename_ << ":" << loc.line << ":" << loc.column << ": error: " << msg;
  throw std::runtime_error(os.str());
}

// ═══════════════════════════════════════════════════════════════════════
//  Type parsing
// ═══════════════════════════════════════════════════════════════════════

TypePtr Parser::parseType() {
  // Pointer: *Type
  if (match(TokenType::STAR)) {
    auto inner = parseType();
    auto node = std::make_unique<PointerType>(std::move(inner));
    return node;
  }

  // Reference: &Type
  if (match(TokenType::AMPERSAND)) {
    auto inner = parseType();
    auto node = std::make_unique<ReferenceType>(std::move(inner));
    return node;
  }

  // Array: [N]Type
  if (match(TokenType::LBRACKET)) {
    ExprPtr sizeExpr = nullptr;
    if (!check(TokenType::RBRACKET)) {
      sizeExpr = parseExpression();
    }
    expect(TokenType::RBRACKET, "expected ']' in array type");
    auto elemType = parseType();
    return std::make_unique<ArrayType>(std::move(elemType),
                                       std::move(sizeExpr));
  }

  // Named / primitive type
  if (check(TokenType::KEYWORD) || check(TokenType::IDENTIFIER)) {
    std::string name = advance().value;

    // Check for generic args:  Type<T1, T2>
    if (check(TokenType::LESS)) {
      // Distinguish generic args from comparison — simple heuristic:
      // if the next token after < is a type-like token, treat as generic
      if (peekAt(1).type == TokenType::KEYWORD ||
          peekAt(1).type == TokenType::IDENTIFIER) {
        advance(); // consume <
        auto namedTy = std::make_unique<NamedType>(name);
        namedTy->typeArgs.push_back(parseType());
        while (match(TokenType::COMMA)) {
          namedTy->typeArgs.push_back(parseType());
        }
        expect(TokenType::GREATER,
               "expected '>' closing generic type arguments");
        return namedTy;
      }
    }

    return std::make_unique<PrimitiveType>(name);
  }

  error("expected type");
}

// ═══════════════════════════════════════════════════════════════════════
//  Program
// ═══════════════════════════════════════════════════════════════════════

std::unique_ptr<Program> Parser::parse() {
  auto program = std::make_unique<Program>();
  while (!check(TokenType::END_OF_FILE)) {
    program->declarations.push_back(parseTopLevel());
  }
  return program;
}

StmtPtr Parser::parseTopLevel() {
  if (checkKw("fn"))
    return parseFunctionDecl();
  if (checkKw("struct"))
    return parseStructDecl();
  if (checkKw("enum"))
    return parseEnumDecl();
  if (checkKw("namespace"))
    return parseNamespaceDecl();
  if (checkKw("import"))
    return parseImportDecl();
  if (checkKw("extern")) {
    advance();                  // consume 'extern'
    auto fn = parseFunctionDecl(); // extern fn ...
    if (auto fd = dynamic_cast<FunctionDecl *>(fn.get())) {
      fd->isExtern = true;
    }
    return fn;
  }
  if (checkKw("let") || checkKw("mut") || checkKw("const")) {
    bool isMut = checkKw("mut");
    auto stmt = parseVariableDecl(isMut);
    // Mark as global
    if (auto vd = dynamic_cast<VariableDecl *>(stmt.get())) {
      vd->isGlobal = true;
    }
    return stmt;
  }
  // using Name = Type;
  if (checkKw("using")) {
    auto loc = peek().loc;
    advance(); // consume 'using'
    std::string name = expect(TokenType::IDENTIFIER, "expected type alias name").value;
    expect(TokenType::EQUAL, "expected '=' after type alias name");
    auto type = parseType();
    expect(TokenType::SEMICOLON, "expected ';' after type alias");
    auto decl = std::make_unique<TypeAliasDecl>(name, std::move(type));
    decl->loc = loc;
    return decl;
  }
  // impl StructName { fn ... }
  if (checkKw("impl")) {
    auto loc = peek().loc;
    advance(); // consume 'impl'
    std::string structName = expect(TokenType::IDENTIFIER, "expected struct name after 'impl'").value;
    expect(TokenType::LBRACE, "expected '{' after struct name in impl");

    auto impl = std::make_unique<ImplBlock>(structName);
    impl->loc = loc;

    while (!check(TokenType::RBRACE) && !check(TokenType::END_OF_FILE)) {
      expectKw("fn", "expected 'fn' in impl block");
      // Rewind to re-parse with parseFunctionDecl (which expects 'fn')
      pos_--; // put 'fn' back
      auto method = parseFunctionDecl();
      if (auto fd = dynamic_cast<FunctionDecl *>(method.get())) {
        impl->methods.push_back(
            std::unique_ptr<FunctionDecl>(static_cast<FunctionDecl *>(method.release())));
      }
    }
    expect(TokenType::RBRACE, "expected '}' closing impl block");
    return impl;
  }

  error("expected top-level declaration (fn, struct, enum, namespace, import, "
        "let, impl)");
}

// ═══════════════════════════════════════════════════════════════════════
//  Function declaration
//  fn name(Type->param, ...) -> RetType { body }
// ═══════════════════════════════════════════════════════════════════════

StmtPtr Parser::parseFunctionDecl() {
  auto loc = peek().loc;
  expectKw("fn", "expected 'fn'");
  std::string name =
      expect(TokenType::IDENTIFIER, "expected function name").value;
  expect(TokenType::LPAREN, "expected '(' after function name");

  // Parse parameters: Type->name [= default]
  std::vector<Parameter> params;
  bool hadDefault = false;
  if (!check(TokenType::RPAREN)) {
    do {
      Parameter p;
      if (checkKw("mut")) {
        advance();
        p.isMutable = true;
      }
      p.type = parseType();
      expect(TokenType::ARROW, "expected '->' between param type and name");
      p.name = expect(TokenType::IDENTIFIER, "expected parameter name").value;
      // Default parameter value
      if (match(TokenType::EQUAL)) {
        p.defaultValue = std::unique_ptr<Expression>(nullptr);
        p.defaultValue = parseExpression();
        hadDefault = true;
      } else if (hadDefault) {
        error("non-default parameter after default parameter");
      }
      params.push_back(std::move(p));
    } while (match(TokenType::COMMA));
  }
  expect(TokenType::RPAREN, "expected ')'");

  // Return type
  TypePtr retType;
  if (match(TokenType::ARROW)) {
    retType = parseType();
  } else {
    retType = std::make_unique<PrimitiveType>("void");
  }

  // Body (optional for extern declarations)
  std::unique_ptr<BlockStmt> body = nullptr;
  if (check(TokenType::LBRACE)) {
    body = parseBlock();
  } else {
    // Extern or forward declaration — expect semicolon
    expect(TokenType::SEMICOLON, "expected '{' or ';' after function declaration");
  }

  auto fn = std::make_unique<FunctionDecl>(name, std::move(params),
                                           std::move(retType), std::move(body));
  fn->loc = loc;
  return fn;
}

// ═══════════════════════════════════════════════════════════════════════
//  Struct declaration
//  struct Name { Type->field; ... }
// ═══════════════════════════════════════════════════════════════════════

StmtPtr Parser::parseStructDecl() {
  auto loc = peek().loc;
  expectKw("struct", "expected 'struct'");
  std::string name =
      expect(TokenType::IDENTIFIER, "expected struct name").value;
  expect(TokenType::LBRACE, "expected '{'");

  std::vector<StructField> fields;
  while (!check(TokenType::RBRACE) && !check(TokenType::END_OF_FILE)) {
    StructField f;
    bool pub = true;
    if (matchKw("priv"))
      pub = false;
    else if (matchKw("pub"))
      pub = true;
    f.isPublic = pub;
    f.type = parseType();
    expect(TokenType::ARROW, "expected '->' between field type and name");
    f.name = expect(TokenType::IDENTIFIER, "expected field name").value;
    expect(TokenType::SEMICOLON, "expected ';' after struct field");
    fields.push_back(std::move(f));
  }
  expect(TokenType::RBRACE, "expected '}'");

  auto decl = std::make_unique<StructDecl>(name, std::move(fields));
  decl->loc = loc;
  return decl;
}

// ═══════════════════════════════════════════════════════════════════════
//  Enum declaration
//  enum Name { Variant1, Variant2 = 5, ... }
// ═══════════════════════════════════════════════════════════════════════

StmtPtr Parser::parseEnumDecl() {
  auto loc = peek().loc;
  expectKw("enum", "expected 'enum'");
  std::string name = expect(TokenType::IDENTIFIER, "expected enum name").value;
  expect(TokenType::LBRACE, "expected '{'");

  std::vector<EnumVariant> variants;
  while (!check(TokenType::RBRACE) && !check(TokenType::END_OF_FILE)) {
    EnumVariant v;
    v.name = expect(TokenType::IDENTIFIER, "expected variant name").value;
    if (match(TokenType::EQUAL)) {
      v.value = std::stoi(
          expect(TokenType::INTEGER_LITERAL, "expected integer value").value);
    }
    if (!check(TokenType::RBRACE)) {
      expect(TokenType::COMMA, "expected ',' between enum variants");
    }
    variants.push_back(std::move(v));
  }
  expect(TokenType::RBRACE, "expected '}'");

  auto decl = std::make_unique<EnumDecl>(name, std::move(variants));
  decl->loc = loc;
  return decl;
}

// ═══════════════════════════════════════════════════════════════════════
//  Namespace  —  namespace name { ... }
// ═══════════════════════════════════════════════════════════════════════

StmtPtr Parser::parseNamespaceDecl() {
  auto loc = peek().loc;
  expectKw("namespace", "expected 'namespace'");
  std::string name =
      expect(TokenType::IDENTIFIER, "expected namespace name").value;
  expect(TokenType::LBRACE, "expected '{'");

  std::vector<StmtPtr> body;
  while (!check(TokenType::RBRACE) && !check(TokenType::END_OF_FILE)) {
    body.push_back(parseTopLevel());
  }
  expect(TokenType::RBRACE, "expected '}'");

  auto decl = std::make_unique<NamespaceDecl>(name, std::move(body));
  decl->loc = loc;
  return decl;
}

// ═══════════════════════════════════════════════════════════════════════
//  Import  —  import std::io;
// ═══════════════════════════════════════════════════════════════════════

StmtPtr Parser::parseImportDecl() {
  auto loc = peek().loc;
  expectKw("import", "expected 'import'");
  std::string path =
      expect(TokenType::IDENTIFIER, "expected module name").value;
  while (match(TokenType::COLON_COLON)) {
    path += "::";
    path +=
        expect(TokenType::IDENTIFIER, "expected module name after '::'").value;
  }
  expect(TokenType::SEMICOLON, "expected ';' after import");
  auto decl = std::make_unique<ImportDecl>(path);
  decl->loc = loc;
  return decl;
}

// ═══════════════════════════════════════════════════════════════════════
//  Block  —  { stmt* }
// ═══════════════════════════════════════════════════════════════════════

std::unique_ptr<BlockStmt> Parser::parseBlock() {
  expect(TokenType::LBRACE, "expected '{'");
  std::vector<StmtPtr> stmts;
  while (!check(TokenType::RBRACE) && !check(TokenType::END_OF_FILE)) {
    stmts.push_back(parseStatement());
  }
  expect(TokenType::RBRACE, "expected '}'");
  return std::make_unique<BlockStmt>(std::move(stmts));
}

// ═══════════════════════════════════════════════════════════════════════
//  Statements
// ═══════════════════════════════════════════════════════════════════════

StmtPtr Parser::parseStatement() {
  if (checkKw("let"))
    return parseVariableDecl(false);
  if (checkKw("mut"))
    return parseVariableDecl(true);
  if (checkKw("const")) {
    advance();
    return parseVariableDecl(false);
  } // TODO: const flag
  if (checkKw("if"))
    return parseIfStmt();
  if (checkKw("while"))
    return parseWhileStmt();
  if (checkKw("for"))
    return parseForStmt();
  if (checkKw("do"))
    return parseDoWhileStmt();
  if (checkKw("switch"))
    return parseSwitchStmt();
  if (checkKw("return"))
    return parseReturnStmt();
  if (checkKw("output"))
    return parseOutputStmt();
  if (checkKw("break")) {
    auto loc = peek().loc;
    advance();
    expect(TokenType::SEMICOLON, "expected ';' after break");
    auto s = std::make_unique<BreakStmt>();
    s->loc = loc;
    return s;
  }
  if (checkKw("continue")) {
    auto loc = peek().loc;
    advance();
    expect(TokenType::SEMICOLON, "expected ';' after continue");
    auto s = std::make_unique<ContinueStmt>();
    s->loc = loc;
    return s;
  }
  // using Name = Type; (at statement level)
  if (checkKw("using")) {
    auto loc = peek().loc;
    advance();
    std::string name = expect(TokenType::IDENTIFIER, "expected type alias name").value;
    expect(TokenType::EQUAL, "expected '=' after type alias name");
    auto type = parseType();
    expect(TokenType::SEMICOLON, "expected ';' after type alias");
    auto decl = std::make_unique<TypeAliasDecl>(name, std::move(type));
    decl->loc = loc;
    return decl;
  }
  if (check(TokenType::LBRACE)) {
    auto b = parseBlock();
    return b;
  }
  return parseExpressionStmt();
}

// ─── Variable declaration ──────────────────────────────────────────────
//  let Type->name = expr;
//  mut Type->name = expr;

StmtPtr Parser::parseVariableDecl(bool isMut) {
  auto loc = peek().loc;
  if (checkKw("let") || checkKw("mut"))
    advance(); // consume let/mut

  // Parse type
  TypePtr type = parseType();

  // ->name
  expect(TokenType::ARROW, "expected '->' between type and variable name");
  std::string name =
      expect(TokenType::IDENTIFIER, "expected variable name").value;

  // Optional initializer
  ExprPtr init = nullptr;
  if (match(TokenType::EQUAL)) {
    init = parseExpression();
  }

  expect(TokenType::SEMICOLON, "expected ';' after variable declaration");

  auto decl = std::make_unique<VariableDecl>(name, std::move(type),
                                             std::move(init), isMut);
  decl->loc = loc;
  return decl;
}

// ─── If statement ──────────────────────────────────────────────────────

StmtPtr Parser::parseIfStmt() {
  auto loc = peek().loc;
  expectKw("if", "expected 'if'");
  expect(TokenType::LPAREN, "expected '(' after 'if'");
  auto cond = parseExpression();
  expect(TokenType::RPAREN, "expected ')' after if condition");

  auto thenBranch = parseStatement();

  StmtPtr elseBranch = nullptr;
  if (matchKw("else")) {
    elseBranch = parseStatement();
  }

  auto stmt = std::make_unique<IfStmt>(std::move(cond), std::move(thenBranch),
                                       std::move(elseBranch));
  stmt->loc = loc;
  return stmt;
}

// ─── While statement ───────────────────────────────────────────────────

StmtPtr Parser::parseWhileStmt() {
  auto loc = peek().loc;
  expectKw("while", "expected 'while'");
  expect(TokenType::LPAREN, "expected '(' after 'while'");
  auto cond = parseExpression();
  expect(TokenType::RPAREN, "expected ')'");
  auto body = parseStatement();
  auto stmt = std::make_unique<WhileStmt>(std::move(cond), std::move(body));
  stmt->loc = loc;
  return stmt;
}

// ─── For statement ─────────────────────────────────────────────────────
//  for (init; cond; incr) { body }

StmtPtr Parser::parseForStmt() {
  auto loc = peek().loc;
  expectKw("for", "expected 'for'");
  expect(TokenType::LPAREN, "expected '(' after 'for'");

  // Init
  StmtPtr init = nullptr;
  if (checkKw("let") || checkKw("mut")) {
    init = parseVariableDecl(checkKw("mut"));
  } else if (!check(TokenType::SEMICOLON)) {
    init = parseExpressionStmt();
  } else {
    advance(); // skip ;
  }

  // Condition
  ExprPtr cond = nullptr;
  if (!check(TokenType::SEMICOLON)) {
    cond = parseExpression();
  }
  expect(TokenType::SEMICOLON, "expected ';' after for condition");

  // Increment
  ExprPtr incr = nullptr;
  if (!check(TokenType::RPAREN)) {
    incr = parseExpression();
  }
  expect(TokenType::RPAREN, "expected ')'");

  auto body = parseStatement();
  auto stmt = std::make_unique<ForStmt>(std::move(init), std::move(cond),
                                        std::move(incr), std::move(body));
  stmt->loc = loc;
  return stmt;
}

// ─── Do-while statement ────────────────────────────────────────────────
//  do { body } while (cond);

StmtPtr Parser::parseDoWhileStmt() {
  auto loc = peek().loc;
  expectKw("do", "expected 'do'");
  auto body = parseStatement();
  expectKw("while", "expected 'while' after do body");
  expect(TokenType::LPAREN, "expected '(' after 'while'");
  auto cond = parseExpression();
  expect(TokenType::RPAREN, "expected ')'");
  expect(TokenType::SEMICOLON, "expected ';' after do-while");
  auto stmt = std::make_unique<DoWhileStmt>(std::move(body), std::move(cond));
  stmt->loc = loc;
  return stmt;
}

// ─── Switch statement ──────────────────────────────────────────────────
//  switch (expr) { case val: stmts; default: stmts; }

StmtPtr Parser::parseSwitchStmt() {
  auto loc = peek().loc;
  expectKw("switch", "expected 'switch'");
  expect(TokenType::LPAREN, "expected '(' after 'switch'");
  auto expr = parseExpression();
  expect(TokenType::RPAREN, "expected ')'");
  expect(TokenType::LBRACE, "expected '{'");

  std::vector<CaseClause> cases;
  while (!check(TokenType::RBRACE) && !check(TokenType::END_OF_FILE)) {
    CaseClause clause;
    if (matchKw("case")) {
      clause.value = parseExpression();
      expect(TokenType::COLON, "expected ':' after case value");
    } else if (matchKw("default")) {
      clause.value = nullptr; // default case
      expect(TokenType::COLON, "expected ':' after 'default'");
    } else {
      error("expected 'case' or 'default' in switch");
    }
    // Parse statements until next case/default/}
    while (!checkKw("case") && !checkKw("default") &&
           !check(TokenType::RBRACE) && !check(TokenType::END_OF_FILE)) {
      clause.body.push_back(parseStatement());
    }
    cases.push_back(std::move(clause));
  }
  expect(TokenType::RBRACE, "expected '}'");

  auto stmt = std::make_unique<SwitchStmt>(std::move(expr), std::move(cases));
  stmt->loc = loc;
  return stmt;
}

// ─── Return ────────────────────────────────────────────────────────────

StmtPtr Parser::parseReturnStmt() {
  auto loc = peek().loc;
  expectKw("return", "expected 'return'");
  ExprPtr val = nullptr;
  if (!check(TokenType::SEMICOLON)) {
    val = parseExpression();
  }
  expect(TokenType::SEMICOLON, "expected ';' after return");
  auto stmt = std::make_unique<ReturnStmt>(std::move(val));
  stmt->loc = loc;
  return stmt;
}

// ─── Output ────────────────────────────────────────────────────────────

StmtPtr Parser::parseOutputStmt() {
  auto loc = peek().loc;
  expectKw("output", "expected 'output'");
  expect(TokenType::LPAREN, "expected '('");
  auto expr = parseExpression();
  expect(TokenType::RPAREN, "expected ')'");
  expect(TokenType::SEMICOLON, "expected ';' after output");
  auto stmt = std::make_unique<OutputStmt>(std::move(expr));
  stmt->loc = loc;
  return stmt;
}

// ─── Expression statement ──────────────────────────────────────────────

StmtPtr Parser::parseExpressionStmt() {
  auto loc = peek().loc;
  auto expr = parseExpression();
  expect(TokenType::SEMICOLON, "expected ';' after expression");
  auto stmt = std::make_unique<ExpressionStmt>(std::move(expr));
  stmt->loc = loc;
  return stmt;
}

// ═══════════════════════════════════════════════════════════════════════
//  EXPRESSION PARSING  (Pratt / precedence climbing)
// ═══════════════════════════════════════════════════════════════════════

ExprPtr Parser::parseExpression() { return parseAssignment(); }

ExprPtr Parser::parseAssignment() {
  auto left = parseTernary();

  // Assignment operators
  if (check(TokenType::EQUAL) || check(TokenType::PLUS_EQUAL) ||
      check(TokenType::MINUS_EQUAL) || check(TokenType::STAR_EQUAL) ||
      check(TokenType::SLASH_EQUAL) || check(TokenType::PERCENT_EQUAL)) {
    auto op = advance().type;
    auto right = parseAssignment(); // right-associative
    auto loc = left->loc;
    auto node =
        std::make_unique<AssignmentExpr>(op, std::move(left), std::move(right));
    node->loc = loc;
    return node;
  }

  return left;
}

ExprPtr Parser::parseTernary() {
  auto cond = parseLogicalOr();
  if (match(TokenType::QUESTION)) {
    auto thenE = parseExpression();
    expect(TokenType::COLON, "expected ':' in ternary expression");
    auto elseE = parseTernary();
    auto loc = cond->loc;
    auto node = std::make_unique<TernaryExpr>(std::move(cond), std::move(thenE),
                                              std::move(elseE));
    node->loc = loc;
    return node;
  }
  return cond;
}

ExprPtr Parser::parseLogicalOr() {
  auto left = parseLogicalAnd();
  while (check(TokenType::PIPE_PIPE)) {
    auto op = advance().type;
    auto right = parseLogicalAnd();
    auto loc = left->loc;
    auto node =
        std::make_unique<BinaryExpr>(op, std::move(left), std::move(right));
    node->loc = loc;
    left = std::move(node);
  }
  return left;
}

ExprPtr Parser::parseLogicalAnd() {
  auto left = parseBitwiseOr();
  while (check(TokenType::AMPERSAND_AMPERSAND)) {
    auto op = advance().type;
    auto right = parseBitwiseOr();
    auto loc = left->loc;
    auto node =
        std::make_unique<BinaryExpr>(op, std::move(left), std::move(right));
    node->loc = loc;
    left = std::move(node);
  }
  return left;
}

ExprPtr Parser::parseBitwiseOr() {
  auto left = parseBitwiseXor();
  while (check(TokenType::PIPE)) {
    auto op = advance().type;
    auto right = parseBitwiseXor();
    auto loc = left->loc;
    auto node =
        std::make_unique<BinaryExpr>(op, std::move(left), std::move(right));
    node->loc = loc;
    left = std::move(node);
  }
  return left;
}

ExprPtr Parser::parseBitwiseXor() {
  auto left = parseBitwiseAnd();
  while (check(TokenType::CARET)) {
    auto op = advance().type;
    auto right = parseBitwiseAnd();
    auto loc = left->loc;
    auto node =
        std::make_unique<BinaryExpr>(op, std::move(left), std::move(right));
    node->loc = loc;
    left = std::move(node);
  }
  return left;
}

ExprPtr Parser::parseBitwiseAnd() {
  auto left = parseEquality();
  while (check(TokenType::AMPERSAND)) {
    auto op = advance().type;
    auto right = parseEquality();
    auto loc = left->loc;
    auto node =
        std::make_unique<BinaryExpr>(op, std::move(left), std::move(right));
    node->loc = loc;
    left = std::move(node);
  }
  return left;
}

ExprPtr Parser::parseEquality() {
  auto left = parseRelational();
  while (check(TokenType::EQUAL_EQUAL) || check(TokenType::EXCLAIM_EQUAL)) {
    auto op = advance().type;
    auto right = parseRelational();
    auto loc = left->loc;
    auto node =
        std::make_unique<BinaryExpr>(op, std::move(left), std::move(right));
    node->loc = loc;
    left = std::move(node);
  }
  return left;
}

ExprPtr Parser::parseRelational() {
  auto left = parseShift();
  while (check(TokenType::LESS) || check(TokenType::GREATER) ||
         check(TokenType::LESS_EQUAL) || check(TokenType::GREATER_EQUAL)) {
    auto op = advance().type;
    auto right = parseShift();
    auto loc = left->loc;
    auto node =
        std::make_unique<BinaryExpr>(op, std::move(left), std::move(right));
    node->loc = loc;
    left = std::move(node);
  }
  return left;
}

ExprPtr Parser::parseShift() {
  auto left = parseAdditive();
  while (check(TokenType::SHL) || check(TokenType::SHR)) {
    auto op = advance().type;
    auto right = parseAdditive();
    auto loc = left->loc;
    auto node =
        std::make_unique<BinaryExpr>(op, std::move(left), std::move(right));
    node->loc = loc;
    left = std::move(node);
  }
  return left;
}

ExprPtr Parser::parseAdditive() {
  auto left = parseMultiplicative();
  while (check(TokenType::PLUS) || check(TokenType::MINUS)) {
    auto op = advance().type;
    auto right = parseMultiplicative();
    auto loc = left->loc;
    auto node =
        std::make_unique<BinaryExpr>(op, std::move(left), std::move(right));
    node->loc = loc;
    left = std::move(node);
  }
  return left;
}

ExprPtr Parser::parseMultiplicative() {
  auto left = parseUnary();
  while (check(TokenType::STAR) || check(TokenType::SLASH) ||
         check(TokenType::PERCENT)) {
    auto op = advance().type;
    auto right = parseUnary();
    auto loc = left->loc;
    auto node =
        std::make_unique<BinaryExpr>(op, std::move(left), std::move(right));
    node->loc = loc;
    left = std::move(node);
  }
  return left;
}

ExprPtr Parser::parseUnary() {
  // Prefix unary: -, !, ~, &, *, ++, --
  if (check(TokenType::MINUS) || check(TokenType::EXCLAIM) ||
      check(TokenType::TILDE) || check(TokenType::AMPERSAND) ||
      check(TokenType::STAR) || check(TokenType::PLUS_PLUS) ||
      check(TokenType::MINUS_MINUS)) {
    auto loc = peek().loc;
    auto op = advance().type;
    auto operand = parseUnary();
    auto node = std::make_unique<UnaryExpr>(op, std::move(operand), true);
    node->loc = loc;
    return node;
  }

  // sizeof(Type)
  if (checkKw("sizeof")) {
    auto loc = peek().loc;
    advance();
    expect(TokenType::LPAREN, "expected '(' after sizeof");
    auto type = parseType();
    expect(TokenType::RPAREN, "expected ')'");
    auto node = std::make_unique<SizeofExpr>(std::move(type));
    node->loc = loc;
    return node;
  }

  // Cast: expr as Type  — handled in postfix

  return parsePostfix();
}

ExprPtr Parser::parsePostfix() {
  auto expr = parsePrimary();

  while (true) {
    auto loc = expr->loc;

    // Function call: expr(args)
    if (check(TokenType::LPAREN)) {
      advance();
      auto args = parseArgList();
      expect(TokenType::RPAREN, "expected ')'");
      auto node = std::make_unique<CallExpr>(std::move(expr), std::move(args));
      node->loc = loc;
      expr = std::move(node);
      continue;
    }

    // Array subscript: expr[index]
    if (check(TokenType::LBRACKET)) {
      advance();
      auto index = parseExpression();
      expect(TokenType::RBRACKET, "expected ']'");
      auto node = std::make_unique<ArraySubscriptExpr>(std::move(expr),
                                                       std::move(index));
      node->loc = loc;
      expr = std::move(node);
      continue;
    }

    // Member access: expr.member
    if (check(TokenType::DOT)) {
      advance();
      std::string member =
          expect(TokenType::IDENTIFIER, "expected member name").value;
      auto node =
          std::make_unique<MemberAccessExpr>(std::move(expr), member, false);
      node->loc = loc;
      expr = std::move(node);
      continue;
    }

    // Arrow access: expr->member  (for pointer dereference member access)
    // Note: we check ARROW token
    if (check(TokenType::ARROW) && peekAt(1).type == TokenType::IDENTIFIER &&
        peekAt(0).type == TokenType::ARROW) {
      // Disambiguate from function return type arrow:
      // This is only valid after an expression, not after a type
      // We'll only treat it as member access in postfix position
      advance(); // consume ->
      std::string member =
          expect(TokenType::IDENTIFIER, "expected member name").value;
      auto node =
          std::make_unique<MemberAccessExpr>(std::move(expr), member, true);
      node->loc = loc;
      expr = std::move(node);
      continue;
    }

    // Postfix ++, --
    if (check(TokenType::PLUS_PLUS) || check(TokenType::MINUS_MINUS)) {
      auto op = advance().type;
      auto node = std::make_unique<UnaryExpr>(op, std::move(expr), false);
      node->loc = loc;
      expr = std::move(node);
      continue;
    }

    // Cast: expr as Type
    if (checkKw("as")) {
      advance();
      auto type = parseType();
      auto node = std::make_unique<CastExpr>(std::move(expr), std::move(type));
      node->loc = loc;
      expr = std::move(node);
      continue;
    }

    break;
  }

  return expr;
}

ExprPtr Parser::parsePrimary() {
  auto loc = peek().loc;

  // Integer literal
  if (check(TokenType::INTEGER_LITERAL)) {
    auto val = advance().value;
    int64_t n;
    if (val.size() > 2 && val[0] == '0' && (val[1] == 'x' || val[1] == 'X'))
      n = std::stoll(val, nullptr, 16);
    else if (val.size() > 2 && val[0] == '0' &&
             (val[1] == 'b' || val[1] == 'B'))
      n = std::stoll(val.substr(2), nullptr, 2);
    else
      n = std::stoll(val);
    auto node = std::make_unique<IntegerLiteral>(n);
    node->loc = loc;
    return node;
  }

  // Float literal
  if (check(TokenType::FLOAT_LITERAL)) {
    double d = std::stod(advance().value);
    auto node = std::make_unique<FloatLiteral>(d);
    node->loc = loc;
    return node;
  }

  // String literal
  if (check(TokenType::STRING_LITERAL)) {
    auto node = std::make_unique<StringLiteral>(advance().value);
    node->loc = loc;
    return node;
  }

  // Char literal
  if (check(TokenType::CHAR_LITERAL)) {
    auto node = std::make_unique<CharLiteral>(advance().value[0]);
    node->loc = loc;
    return node;
  }

  // Boolean literals
  if (checkKw("true")) {
    advance();
    auto n = std::make_unique<BoolLiteral>(true);
    n->loc = loc;
    return n;
  }
  if (checkKw("false")) {
    advance();
    auto n = std::make_unique<BoolLiteral>(false);
    n->loc = loc;
    return n;
  }

  // Null literal
  if (checkKw("null")) {
    advance();
    auto n = std::make_unique<NullLiteral>();
    n->loc = loc;
    return n;
  }

  // input() expression — reads int from stdin
  if (checkKw("input")) {
    advance();
    expect(TokenType::LPAREN, "expected '(' after 'input'");
    expect(TokenType::RPAREN, "expected ')' after 'input('");
    auto node = std::make_unique<InputExpr>();
    node->loc = loc;
    return node;
  }

  // Identifier (possibly with :: scope resolution)
  if (check(TokenType::IDENTIFIER)) {
    std::string name = advance().value;

    // Scope resolution: Name::member
    if (check(TokenType::COLON_COLON)) {
      advance();
      std::string member =
          expect(TokenType::IDENTIFIER, "expected identifier after '::'").value;
      auto node = std::make_unique<ScopeResolutionExpr>(name, member);
      node->loc = loc;
      return node;
    }

    auto node = std::make_unique<IdentifierExpr>(name);
    node->loc = loc;
    return node;
  }

  // Parenthesized expression
  if (match(TokenType::LPAREN)) {
    auto expr = parseExpression();
    expect(TokenType::RPAREN, "expected ')'");
    return expr;
  }

  // Array literal: [expr, expr, ...]
  if (match(TokenType::LBRACKET)) {
    std::vector<ExprPtr> elems;
    if (!check(TokenType::RBRACKET)) {
      elems.push_back(parseExpression());
      while (match(TokenType::COMMA)) {
        elems.push_back(parseExpression());
      }
    }
    expect(TokenType::RBRACKET, "expected ']'");
    auto node = std::make_unique<ArrayLiteralExpr>(std::move(elems));
    node->loc = loc;
    return node;
  }

  // new operator: new Type(args)
  if (checkKw("new")) {
    advance();
    auto type = parseType();
    std::vector<ExprPtr> args;
    if (match(TokenType::LPAREN)) {
      args = parseArgList();
      expect(TokenType::RPAREN, "expected ')'");
    }
    auto node = std::make_unique<NewExpr>(std::move(type), std::move(args));
    node->loc = loc;
    return node;
  }

  // delete operator
  if (checkKw("delete")) {
    advance();
    auto expr = parseUnary();
    auto node = std::make_unique<DeleteExpr>(std::move(expr));
    node->loc = loc;
    return node;
  }

  error("unexpected token in expression: '" + peek().value + "'");
}

// ─── Argument list parsing ─────────────────────────────────────────────

std::vector<ExprPtr> Parser::parseArgList() {
  std::vector<ExprPtr> args;
  if (!check(TokenType::RPAREN)) {
    args.push_back(parseExpression());
    while (match(TokenType::COMMA)) {
      args.push_back(parseExpression());
    }
  }
  return args;
}
#pragma once

#include "ast.hpp"
#include "lexer.hpp"
#include <memory>
#include <string>
#include <vector>

class Parser {
public:
  explicit Parser(const std::vector<Token> &tokens,
                  const std::string &filename = "<stdin>");
  std::unique_ptr<Program> parse();

private:
  const std::vector<Token> &tokens_;
  std::string filename_;
  size_t pos_ = 0;

  // ── Token helpers ──────────────────────────────────────────────────
  const Token &peek() const;
  const Token &peekAt(size_t offset) const;
  const Token &advance();
  bool check(TokenType ty) const;
  bool checkKw(const std::string &kw) const;
  bool match(TokenType ty);
  bool matchKw(const std::string &kw);
  const Token &expect(TokenType ty, const std::string &msg);
  void expectKw(const std::string &kw, const std::string &msg);
  [[noreturn]] void error(const std::string &msg) const;

  // ── Type parsing ───────────────────────────────────────────────────
  TypePtr parseType();

  // ── Top-level ──────────────────────────────────────────────────────
  StmtPtr parseTopLevel();
  StmtPtr parseFunctionDecl();
  StmtPtr parseStructDecl();
  StmtPtr parseEnumDecl();
  StmtPtr parseNamespaceDecl();
  StmtPtr parseImportDecl();

  // ── Statements ─────────────────────────────────────────────────────
  StmtPtr parseStatement();
  std::unique_ptr<BlockStmt> parseBlock();
  StmtPtr parseVariableDecl(bool isMut);
  StmtPtr parseIfStmt();
  StmtPtr parseWhileStmt();
  StmtPtr parseForStmt();
  StmtPtr parseDoWhileStmt();
  StmtPtr parseSwitchStmt();
  StmtPtr parseReturnStmt();
  StmtPtr parseOutputStmt();
  StmtPtr parseExpressionStmt();

  // ── Expressions (Pratt parser) ─────────────────────────────────────
  ExprPtr parseExpression();
  ExprPtr parseAssignment();
  ExprPtr parseTernary();
  ExprPtr parseLogicalOr();
  ExprPtr parseLogicalAnd();
  ExprPtr parseBitwiseOr();
  ExprPtr parseBitwiseXor();
  ExprPtr parseBitwiseAnd();
  ExprPtr parseEquality();
  ExprPtr parseRelational();
  ExprPtr parseShift();
  ExprPtr parseAdditive();
  ExprPtr parseMultiplicative();
  ExprPtr parseUnary();
  ExprPtr parsePostfix();
  ExprPtr parsePrimary();

  // ── Helpers ────────────────────────────────────────────────────────
  std::vector<ExprPtr> parseArgList();
};
#pragma once
#include <string>
#include <vector>

// ─── Token types ───────────────────────────────────────────────────────
enum class TokenType {
  // Literals
  INTEGER_LITERAL,
  FLOAT_LITERAL,
  STRING_LITERAL,
  CHAR_LITERAL,

  // Identifier & keywords
  IDENTIFIER,
  KEYWORD,

  // Operators
  PLUS,
  MINUS,
  STAR,
  SLASH,
  PERCENT, // + - * / %
  AMPERSAND,
  PIPE,
  CARET,
  TILDE, // & | ^ ~
  EXCLAIM,
  QUESTION, // ! ?
  EQUAL,
  PLUS_EQUAL,
  MINUS_EQUAL, // = += -=
  STAR_EQUAL,
  SLASH_EQUAL,
  PERCENT_EQUAL, // *= /= %=
  AMPERSAND_EQUAL,
  PIPE_EQUAL,
  CARET_EQUAL, // &= |= ^=
  SHL_EQUAL,
  SHR_EQUAL, // <<= >>=
  EQUAL_EQUAL,
  EXCLAIM_EQUAL, // == !=
  LESS,
  GREATER,
  LESS_EQUAL,
  GREATER_EQUAL, // < > <= >=
  AMPERSAND_AMPERSAND,
  PIPE_PIPE, // && ||
  SHL,
  SHR, // << >>
  PLUS_PLUS,
  MINUS_MINUS, // ++ --
  ARROW,       // ->
  COLON_COLON, // ::
  DOT,         // .
  ELLIPSIS,    // ...

  // Delimiters
  LPAREN,
  RPAREN, // ( )
  LBRACE,
  RBRACE, // { }
  LBRACKET,
  RBRACKET, // [ ]
  SEMICOLON,
  COMMA,
  COLON, // ; , :

  // Special
  END_OF_FILE,
  INVALID
};

// ─── Source location ───────────────────────────────────────────────────
struct SourceLocation {
  int line = 1;
  int column = 1;
};

// ─── Token ─────────────────────────────────────────────────────────────
struct Token {
  TokenType type;
  std::string value;
  SourceLocation loc;
};

// ─── Keyword / type checks ────────────────────────────────────────────
bool isKeyword(const std::string &word);
bool isTypeName(const std::string &word);

// ─── Lexer ─────────────────────────────────────────────────────────────
class Lexer {
public:
  explicit Lexer(const std::string &source,
                 const std::string &filename = "<stdin>");
  std::vector<Token> tokenize();

private:
  std::string source_;
  std::string filename_;
  size_t pos_ = 0;
  int line_ = 1;
  int col_ = 1;

  char peek() const;
  char peekNext() const;
  char advance();
  bool atEnd() const;
  void skipWhitespaceAndComments();
  SourceLocation currentLoc() const;

  Token lexNumber();
  Token lexString();
  Token lexChar();
  Token lexIdentifierOrKeyword();
  Token lexOperatorOrSymbol();
};
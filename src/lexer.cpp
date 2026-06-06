#include "lexer.hpp"
#include <cctype>
#include <stdexcept>
#include <unordered_set>

// ─── Keyword table ─────────────────────────────────────────────────────
static const std::unordered_set<std::string> kKeywords = {
    "fn",
    "let",
    "mut",
    "const",
    "static",
    "if",
    "else",
    "while",
    "for",
    "loop",
    "do",
    "switch",
    "case",
    "default",
    "match",
    "return",
    "break",
    "continue",
    "struct",
    "enum",
    "impl",
    "class",
    "namespace",
    "import",
    "module",
    "pub",
    "priv",
    "extern",
    "as",
    "new",
    "delete",
    "sizeof",
    "typeof",
    "template",
    "ref",
    "typedef",
    "using",
    "goto",
    "true",
    "false",
    "null",
    "output",
    "input",
    // Type keywords
    "int",
    "i8",
    "i16",
    "i32",
    "i64",
    "u8",
    "u16",
    "u32",
    "u64",
    "f32",
    "f64",
    "bool",
    "char",
    "str",
    "void",
    "auto",
};

bool isKeyword(const std::string &w) { return kKeywords.count(w) != 0; }

static const std::unordered_set<std::string> kTypeNames = {
    "int", "i8",  "i16", "i32",  "i64",  "u8",  "u16",  "u32",
    "u64", "f32", "f64", "bool", "char", "str", "void", "auto",
};
bool isTypeName(const std::string &w) { return kTypeNames.count(w) != 0; }

// ─── Lexer implementation ──────────────────────────────────────────────

Lexer::Lexer(const std::string &source, const std::string &filename)
    : source_(source), filename_(filename) {}

char Lexer::peek() const {
  if (pos_ >= source_.size())
    return '\0';
  return source_[pos_];
}

char Lexer::peekNext() const {
  if (pos_ + 1 >= source_.size())
    return '\0';
  return source_[pos_ + 1];
}

char Lexer::advance() {
  char c = source_[pos_++];
  if (c == '\n') {
    line_++;
    col_ = 1;
  } else {
    col_++;
  }
  return c;
}

bool Lexer::atEnd() const { return pos_ >= source_.size(); }

SourceLocation Lexer::currentLoc() const { return {line_, col_}; }

void Lexer::skipWhitespaceAndComments() {
  while (!atEnd()) {
    // Whitespace
    if (std::isspace(peek())) {
      advance();
      continue;
    }

    // Line comment: //
    if (peek() == '/' && peekNext() == '/') {
      advance();
      advance();
      while (!atEnd() && peek() != '\n')
        advance();
      continue;
    }

    // Block comment: /* ... */
    if (peek() == '/' && peekNext() == '*') {
      auto loc = currentLoc();
      advance();
      advance();
      int depth = 1;
      while (!atEnd() && depth > 0) {
        if (peek() == '/' && peekNext() == '*') {
          advance();
          advance();
          depth++;
        } else if (peek() == '*' && peekNext() == '/') {
          advance();
          advance();
          depth--;
        } else
          advance();
      }
      if (depth != 0)
        throw std::runtime_error(filename_ + ":" + std::to_string(loc.line) +
                                 ":" + std::to_string(loc.column) +
                                 ": unterminated block comment");
      continue;
    }

    break;
  }
}

// ─── Number literals ───────────────────────────────────────────────────

Token Lexer::lexNumber() {
  auto loc = currentLoc();
  std::string num;
  bool isFloat = false;

  // Hex: 0x...
  if (peek() == '0' && (peekNext() == 'x' || peekNext() == 'X')) {
    num += advance();
    num += advance(); // 0x
    while (!atEnd() && (std::isxdigit(peek()) || peek() == '_')) {
      if (peek() != '_')
        num += peek();
      advance();
    }
    return {TokenType::INTEGER_LITERAL, num, loc};
  }

  // Binary: 0b...
  if (peek() == '0' && (peekNext() == 'b' || peekNext() == 'B')) {
    num += advance();
    num += advance(); // 0b
    while (!atEnd() && (peek() == '0' || peek() == '1' || peek() == '_')) {
      if (peek() != '_')
        num += peek();
      advance();
    }
    return {TokenType::INTEGER_LITERAL, num, loc};
  }

  // Decimal / float
  while (!atEnd() && (std::isdigit(peek()) || peek() == '_')) {
    if (peek() != '_')
      num += peek();
    advance();
  }

  if (!atEnd() && peek() == '.' && std::isdigit(peekNext())) {
    isFloat = true;
    num += advance(); // '.'
    while (!atEnd() && (std::isdigit(peek()) || peek() == '_')) {
      if (peek() != '_')
        num += peek();
      advance();
    }
  }

  // Exponent
  if (!atEnd() && (peek() == 'e' || peek() == 'E')) {
    isFloat = true;
    num += advance();
    if (!atEnd() && (peek() == '+' || peek() == '-'))
      num += advance();
    while (!atEnd() && std::isdigit(peek()))
      num += advance();
  }

  // Float suffix f
  if (!atEnd() && (peek() == 'f' || peek() == 'F')) {
    isFloat = true;
    advance(); // consume suffix, don't add to value
  }

  return {isFloat ? TokenType::FLOAT_LITERAL : TokenType::INTEGER_LITERAL, num,
          loc};
}

// ─── String literal ────────────────────────────────────────────────────

Token Lexer::lexString() {
  auto loc = currentLoc();
  advance(); // opening "
  std::string str;
  while (!atEnd() && peek() != '"') {
    if (peek() == '\\') {
      advance();
      switch (peek()) {
      case 'n':
        str += '\n';
        break;
      case 't':
        str += '\t';
        break;
      case 'r':
        str += '\r';
        break;
      case '\\':
        str += '\\';
        break;
      case '"':
        str += '"';
        break;
      case '0':
        str += '\0';
        break;
      default:
        throw std::runtime_error(filename_ + ":" + std::to_string(loc.line) +
                                 ": invalid escape sequence");
      }
      advance();
    } else {
      str += advance();
    }
  }
  if (atEnd())
    throw std::runtime_error(filename_ + ":" + std::to_string(loc.line) +
                             ": unterminated string literal");
  advance(); // closing "
  return {TokenType::STRING_LITERAL, str, loc};
}

// ─── Char literal ──────────────────────────────────────────────────────

Token Lexer::lexChar() {
  auto loc = currentLoc();
  advance(); // opening '
  std::string ch;
  if (peek() == '\\') {
    advance();
    switch (peek()) {
    case 'n':
      ch += '\n';
      break;
    case 't':
      ch += '\t';
      break;
    case 'r':
      ch += '\r';
      break;
    case '\\':
      ch += '\\';
      break;
    case '\'':
      ch += '\'';
      break;
    case '0':
      ch += '\0';
      break;
    default:
      throw std::runtime_error(filename_ + ":" + std::to_string(loc.line) +
                               ": invalid char escape");
    }
    advance();
  } else {
    ch += advance();
  }
  if (atEnd() || peek() != '\'')
    throw std::runtime_error(filename_ + ":" + std::to_string(loc.line) +
                             ": unterminated char literal");
  advance(); // closing '
  return {TokenType::CHAR_LITERAL, ch, loc};
}

// ─── Identifier / keyword ──────────────────────────────────────────────

Token Lexer::lexIdentifierOrKeyword() {
  auto loc = currentLoc();
  std::string word;
  while (!atEnd() && (std::isalnum(peek()) || peek() == '_'))
    word += advance();

  TokenType ty = isKeyword(word) ? TokenType::KEYWORD : TokenType::IDENTIFIER;
  return {ty, word, loc};
}

// ─── Operators / symbols ───────────────────────────────────────────────

Token Lexer::lexOperatorOrSymbol() {
  auto loc = currentLoc();
  char c = peek();

  auto make = [&](TokenType ty, int len) -> Token {
    std::string v;
    for (int i = 0; i < len; i++)
      v += advance();
    return {ty, v, loc};
  };

  switch (c) {
  case '(':
    return make(TokenType::LPAREN, 1);
  case ')':
    return make(TokenType::RPAREN, 1);
  case '{':
    return make(TokenType::LBRACE, 1);
  case '}':
    return make(TokenType::RBRACE, 1);
  case '[':
    return make(TokenType::LBRACKET, 1);
  case ']':
    return make(TokenType::RBRACKET, 1);
  case ';':
    return make(TokenType::SEMICOLON, 1);
  case ',':
    return make(TokenType::COMMA, 1);
  case '~':
    return make(TokenType::TILDE, 1);
  case '?':
    return make(TokenType::QUESTION, 1);

  case '.':
    if (peekNext() == '.' && pos_ + 2 < source_.size() &&
        source_[pos_ + 2] == '.')
      return make(TokenType::ELLIPSIS, 3);
    return make(TokenType::DOT, 1);

  case ':':
    if (peekNext() == ':')
      return make(TokenType::COLON_COLON, 2);
    return make(TokenType::COLON, 1);

  case '+':
    if (peekNext() == '+')
      return make(TokenType::PLUS_PLUS, 2);
    if (peekNext() == '=')
      return make(TokenType::PLUS_EQUAL, 2);
    return make(TokenType::PLUS, 1);

  case '-':
    if (peekNext() == '-')
      return make(TokenType::MINUS_MINUS, 2);
    if (peekNext() == '=')
      return make(TokenType::MINUS_EQUAL, 2);
    if (peekNext() == '>')
      return make(TokenType::ARROW, 2);
    return make(TokenType::MINUS, 1);

  case '*':
    if (peekNext() == '=')
      return make(TokenType::STAR_EQUAL, 2);
    return make(TokenType::STAR, 1);

  case '/':
    if (peekNext() == '=')
      return make(TokenType::SLASH_EQUAL, 2);
    return make(TokenType::SLASH, 1);

  case '%':
    if (peekNext() == '=')
      return make(TokenType::PERCENT_EQUAL, 2);
    return make(TokenType::PERCENT, 1);

  case '=':
    if (peekNext() == '=')
      return make(TokenType::EQUAL_EQUAL, 2);
    if (peekNext() == '>') {
      advance();
      advance();
      return {TokenType::ARROW, "=>", loc};
    }
    return make(TokenType::EQUAL, 1);

  case '!':
    if (peekNext() == '=')
      return make(TokenType::EXCLAIM_EQUAL, 2);
    return make(TokenType::EXCLAIM, 1);

  case '<':
    if (peekNext() == '<') {
      if (pos_ + 2 < source_.size() && source_[pos_ + 2] == '=')
        return make(TokenType::SHL_EQUAL, 3);
      return make(TokenType::SHL, 2);
    }
    if (peekNext() == '=')
      return make(TokenType::LESS_EQUAL, 2);
    return make(TokenType::LESS, 1);

  case '>':
    if (peekNext() == '>') {
      if (pos_ + 2 < source_.size() && source_[pos_ + 2] == '=')
        return make(TokenType::SHR_EQUAL, 3);
      return make(TokenType::SHR, 2);
    }
    if (peekNext() == '=')
      return make(TokenType::GREATER_EQUAL, 2);
    return make(TokenType::GREATER, 1);

  case '&':
    if (peekNext() == '&')
      return make(TokenType::AMPERSAND_AMPERSAND, 2);
    if (peekNext() == '=')
      return make(TokenType::AMPERSAND_EQUAL, 2);
    return make(TokenType::AMPERSAND, 1);

  case '|':
    if (peekNext() == '|')
      return make(TokenType::PIPE_PIPE, 2);
    if (peekNext() == '=')
      return make(TokenType::PIPE_EQUAL, 2);
    return make(TokenType::PIPE, 1);

  case '^':
    if (peekNext() == '=')
      return make(TokenType::CARET_EQUAL, 2);
    return make(TokenType::CARET, 1);

  default:
    break;
  }

  // Unknown character
  std::string bad(1, advance());
  return {TokenType::INVALID, bad, loc};
}

// ─── Main tokenize ────────────────────────────────────────────────────

std::vector<Token> Lexer::tokenize() {
  std::vector<Token> tokens;

  while (!atEnd()) {
    skipWhitespaceAndComments();
    if (atEnd())
      break;

    char c = peek();

    if (std::isdigit(c))
      tokens.push_back(lexNumber());
    else if (c == '"')
      tokens.push_back(lexString());
    else if (c == '\'')
      tokens.push_back(lexChar());
    else if (std::isalpha(c) || c == '_')
      tokens.push_back(lexIdentifierOrKeyword());
    else
      tokens.push_back(lexOperatorOrSymbol());
  }

  tokens.push_back({TokenType::END_OF_FILE, "", currentLoc()});
  return tokens;
}
#include <iostream>
#include <string>
#include <vector>

namespace flux {

enum class TokenKind {
  // Keywords
  KwFn,
  kwIf,
  KwFor,
  KwWhile,
  KwTrait,
  KwApply,
  KwTo,
  KwFalse,
  KwTrue,
  KwLet,
  KwVar,
  KwStruct,
  KwType,
  KwReturn,
  KwMatch,

  // literals
  IntegerLiteral,
  FloatLiteral,
  StringLiteral,
  Identifier,

  // punctuation
  LBracket,
  RBracket,
  LBraces,
  RBraces,
  Arrow,
  BigArrow,
  Comma,
  Dot,
  Colon,
  SemiColon,

  // special
  EndOfFile,
  Invalid,

  // operators
  Plus,
  Minus,
  Star,
  Slash,
  Equal,
  DEqual,
  Bang,
  BangEqual,
  Less,
  LessEqual,
  Greater,
  GreaterEqual

};

struct Token {
  std::string val;
  size_t row;
  size_t col;
};

class Scanner {

private:
  std::vector<Token> tokens;

public:
  void scan();
  void scan_integer();
  void scan_float();
  void scan_string();
};

} // namespace flux

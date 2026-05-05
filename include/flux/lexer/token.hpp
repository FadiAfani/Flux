#pragma once

#include <cstddef>
#include <string>

namespace flux {

enum class TokenKind {
  // Keywords
  KwFn,
  KwIf,
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

  // Literals
  IntegerLiteral,
  FloatLiteral,
  StringLiteral,
  Identifier,

  // Punctuation
  LBracket,
  RBracket,
  LBrace,
  RBrace,
  Arrow,
  BigArrow,
  Comma,
  Dot,
  Colon,
  SemiColon,

  // Special
  EndOfFile,
  Invalid,

  // Operators
  Plus,
  Minus,
  Star,
  Slash,
  Equal,
  DoubleEqual,
  Bang,
  BangEqual,
  Less,
  LessEqual,
  Greater,
  GreaterEqual,
  Error,
};

struct SourceLocation {
  std::size_t offset = 0;
  std::size_t line = 1;
  std::size_t column = 1;
};

struct Token {
  TokenKind kind = TokenKind::Invalid;
  std::string lexeme;
  SourceLocation location;
};

struct SourceSpan {
  SourceLocation start;
  SourceLocation end;
};

} // namespace flux

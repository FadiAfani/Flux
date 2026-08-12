#pragma once

#include <cstddef>
#include <string>

namespace flux {

enum class TokenKind {
  // Keywords
  KwAs,
  KwBreak,
  KwCapability,
  KwConst,
  KwContinue,
  KwDomain,
  KwEffect,
  KwElse,
  KwEnsures,
  KwExists,
  KwExternal,
  KwFalse,
  KwFn,
  KwFor,
  KwForall,
  KwIf,
  KwImpl,
  KwImport,
  KwIn,
  KwInvariant,
  KwLaw,
  KwLet,
  KwMatch,
  KwModule,
  KwMut,
  KwMutate,
  KwOld,
  KwParallel,
  KwPub,
  KwRequires,
  KwResult,
  KwReturn,
  KwSelf,
  KwTotal,
  KwTrait,
  KwTransaction,
  KwTrue,
  KwTrusted,
  KwType,
  KwUnsafe,
  KwUses,
  KwVar,
  KwWhere,
  KwWhile,
  KwWith,
  KwKindType,

  // Literals and names
  IntegerLiteral,
  FloatLiteral,
  CharacterLiteral,
  StringLiteral,
  Identifier,

  // Delimiters and punctuation
  LBracket,
  RBracket,
  LParen,
  RParen,
  LBrace,
  RBrace,
  Comma,
  Dot,
  Colon,
  SemiColon,
  Question,

  // Operators
  Equal,
  PlusEqual,
  MinusEqual,
  StarEqual,
  SlashEqual,
  PercentEqual,
  Plus,
  Minus,
  Star,
  Slash,
  Percent,
  DoubleEqual,
  Bang,
  BangEqual,
  Less,
  LessEqual,
  Greater,
  GreaterEqual,
  LogicalAnd,
  LogicalOr,
  Arrow,
  BigArrow,
  Equivalence,
  Pipe,
  DotDot,
  DotDotEqual,

  // Special
  EndOfFile,
  Invalid,
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

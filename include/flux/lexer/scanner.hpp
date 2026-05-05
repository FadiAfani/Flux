#pragma once

#include <cstddef>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "./token.hpp"

namespace flux::lexer {

inline const std::unordered_map<std::string_view, TokenKind> keywords = {
    {"fn", TokenKind::KwFn},
    {"if", TokenKind::KwIf},
    {"for", TokenKind::KwFor},
    {"while", TokenKind::KwWhile},
    {"trait", TokenKind::KwTrait},
    {"apply", TokenKind::KwApply},
    {"to", TokenKind::KwTo},
    {"false", TokenKind::KwFalse},
    {"true", TokenKind::KwTrue},
    {"let", TokenKind::KwLet},
    {"var", TokenKind::KwVar},
    {"struct", TokenKind::KwStruct},
    {"type", TokenKind::KwType},
    {"return", TokenKind::KwReturn},
    {"match", TokenKind::KwMatch},
};

struct LexError {
  SourceSpan span;
  std::string message;
};

struct ScanResult {
  std::vector<Token> tokens;
  std::vector<LexError> errors;
};

class Scanner {
public:
  explicit Scanner(std::string_view source);

  ScanResult scan();

private:
  std::string_view source_;
  std::size_t cursor_ = 0;
  std::size_t line_ = 1;
  std::size_t column_ = 1;

  std::vector<Token> tokens_;
  std::vector<LexError> errors_;

  bool is_at_end() const;
  char peek() const;
  char peek_next() const;
  char advance();
  bool match(char expected);
  void skip_whitespace();

  Token scan_token();
  Token scan_identifier_or_keyword();
  Token scan_number();
  Token scan_string();
  inline Token init_token(TokenKind kind);
};

} // namespace flux::lexer

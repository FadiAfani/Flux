#pragma once

#include <cstddef>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "token.hpp"

namespace flux::lexer {

inline const std::unordered_map<std::string_view, TokenKind> keywords = {
    {"as", TokenKind::KwAs},
    {"break", TokenKind::KwBreak},
    {"capability", TokenKind::KwCapability},
    {"const", TokenKind::KwConst},
    {"continue", TokenKind::KwContinue},
    {"domain", TokenKind::KwDomain},
    {"effect", TokenKind::KwEffect},
    {"else", TokenKind::KwElse},
    {"ensures", TokenKind::KwEnsures},
    {"exists", TokenKind::KwExists},
    {"external", TokenKind::KwExternal},
    {"false", TokenKind::KwFalse},
    {"fn", TokenKind::KwFn},
    {"for", TokenKind::KwFor},
    {"forall", TokenKind::KwForall},
    {"if", TokenKind::KwIf},
    {"impl", TokenKind::KwImpl},
    {"import", TokenKind::KwImport},
    {"in", TokenKind::KwIn},
    {"invariant", TokenKind::KwInvariant},
    {"law", TokenKind::KwLaw},
    {"let", TokenKind::KwLet},
    {"match", TokenKind::KwMatch},
    {"module", TokenKind::KwModule},
    {"mut", TokenKind::KwMut},
    {"mutate", TokenKind::KwMutate},
    {"old", TokenKind::KwOld},
    {"parallel", TokenKind::KwParallel},
    {"pub", TokenKind::KwPub},
    {"requires", TokenKind::KwRequires},
    {"result", TokenKind::KwResult},
    {"return", TokenKind::KwReturn},
    {"self", TokenKind::KwSelf},
    {"total", TokenKind::KwTotal},
    {"trait", TokenKind::KwTrait},
    {"transaction", TokenKind::KwTransaction},
    {"true", TokenKind::KwTrue},
    {"trusted", TokenKind::KwTrusted},
    {"type", TokenKind::KwType},
    {"unsafe", TokenKind::KwUnsafe},
    {"uses", TokenKind::KwUses},
    {"var", TokenKind::KwVar},
    {"where", TokenKind::KwWhere},
    {"while", TokenKind::KwWhile},
    {"with", TokenKind::KwWith},
    {"Type", TokenKind::KwKindType},
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
  void skip_trivia();
  void skip_block_comment(SourceLocation start);
  void report(SourceLocation start, std::string message);

  Token scan_token();
  Token scan_identifier_or_keyword();
  Token scan_number();
  Token scan_quoted(char quote, TokenKind kind);
  Token init_token(TokenKind kind) const;
};

} // namespace flux::lexer

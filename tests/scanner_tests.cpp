#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

#include "flux/lexer/scanner.hpp"

using flux::Token;
using flux::TokenKind;
using flux::lexer::Scanner;

namespace {

void fail(std::string_view message) {
  std::cerr << message << '\n';
  std::exit(EXIT_FAILURE);
}

std::vector<TokenKind> kinds_of(const std::vector<Token> &tokens) {
  std::vector<TokenKind> kinds;
  kinds.reserve(tokens.size());

  for (const Token &token : tokens) {
    kinds.push_back(token.kind);
  }

  return kinds;
}

void expect_kinds(std::string_view source,
                  const std::vector<TokenKind> &expected) {
  Scanner scanner(source);
  const flux::lexer::ScanResult result = scanner.scan();

  if (!result.errors.empty()) {
    fail("expected no scanner errors");
  }

  if (kinds_of(result.tokens) != expected) {
    fail("token kinds did not match expected sequence");
  }
}

void lexes_keywords_identifiers_and_literals() {
  Scanner scanner("answer = 42\nvar ratio = 3.14 \"hello\"");
  const flux::lexer::ScanResult result = scanner.scan();

  if (!result.errors.empty()) {
    fail("expected literals test to have no errors");
  }

  const std::vector<TokenKind> expected = {
      TokenKind::Identifier,     TokenKind::Equal,
      TokenKind::IntegerLiteral, TokenKind::KwVar,
      TokenKind::Identifier,     TokenKind::Equal,
      TokenKind::FloatLiteral,   TokenKind::StringLiteral,
      TokenKind::EndOfFile,
  };

  if (kinds_of(result.tokens) != expected) {
    fail("keywords, identifiers, and literals were not scanned as expected");
  }

  if (result.tokens[0].lexeme != "answer" || result.tokens[2].lexeme != "42" ||
      result.tokens[6].lexeme != "3.14" || result.tokens[7].lexeme != "hello") {
    fail("literal or identifier lexeme did not match");
  }
}

void lexes_module_keywords() {
  expect_kinds(
      "module import pub trusted external total mut requires ensures uses",
      {
          TokenKind::KwModule,
          TokenKind::KwImport,
          TokenKind::KwPub,
          TokenKind::KwTrusted,
          TokenKind::KwExternal,
          TokenKind::KwTotal,
          TokenKind::KwMut,
          TokenKind::KwRequires,
          TokenKind::KwEnsures,
          TokenKind::KwUses,
          TokenKind::EndOfFile,
      });
}

void lexes_operators_and_punctuation() {
  expect_kinds(
      "== = => -> != ! <= < >= > + - * / | [](){},.:;",
      {
          TokenKind::DoubleEqual, TokenKind::Equal,     TokenKind::BigArrow,
          TokenKind::Arrow,       TokenKind::BangEqual, TokenKind::Bang,
          TokenKind::LessEqual,   TokenKind::Less,      TokenKind::GreaterEqual,
          TokenKind::Greater,     TokenKind::Plus,      TokenKind::Minus,
          TokenKind::Star,        TokenKind::Slash,     TokenKind::Pipe,
          TokenKind::LBracket,    TokenKind::RBracket,  TokenKind::LParen,
          TokenKind::RParen,      TokenKind::LBrace,    TokenKind::RBrace,
          TokenKind::Comma,       TokenKind::Dot,       TokenKind::Colon,
          TokenKind::SemiColon,   TokenKind::EndOfFile,
      });
}

void tracks_locations_after_whitespace() {
  Scanner scanner(" \t\nname");
  const flux::lexer::ScanResult result = scanner.scan();

  if (!result.errors.empty()) {
    fail("expected location test to have no errors");
  }

  if (result.tokens.size() != 2 ||
      result.tokens[0].kind != TokenKind::Identifier ||
      result.tokens[0].lexeme != "name") {
    fail("expected identifier followed by EOF");
  }

  if (result.tokens[0].location.line != 2 ||
      result.tokens[0].location.column != 1) {
    fail("token location after whitespace was incorrect");
  }
}

void reports_unexpected_characters() {
  Scanner scanner("@");
  const flux::lexer::ScanResult result = scanner.scan();

  if (result.tokens.size() != 2 || result.tokens[0].kind != TokenKind::Error ||
      result.tokens[0].lexeme != "@" ||
      result.tokens[1].kind != TokenKind::EndOfFile) {
    fail("unexpected character did not produce error token followed by EOF");
  }

  if (result.errors.size() != 1 ||
      result.errors[0].message != "Unexpected character") {
    fail("unexpected character diagnostic was not emitted");
  }
}

void reports_unterminated_strings() {
  Scanner scanner("\"unterminated\nnext");
  const flux::lexer::ScanResult result = scanner.scan();

  if (result.tokens.empty() || result.tokens[0].kind != TokenKind::Error) {
    fail("unterminated string did not produce an error token");
  }

  if (result.errors.size() != 1 ||
      result.errors[0].message != "Unterminated string literal") {
    fail("unterminated string diagnostic was not emitted");
  }
}

void lexes_the_complete_operator_and_keyword_surface() {
  expect_kinds(
      "as break capability const continue domain effect else ensures exists "
      "external false fn for forall if impl import in invariant law let match "
      "module mut mutate old parallel pub requires result return self total "
      "trait "
      "transaction true trusted type unsafe uses var where while with Type "
      "+= -= *= /= %= % && || <=> .. ..= ?",
      {
          TokenKind::KwAs,         TokenKind::KwBreak,
          TokenKind::KwCapability, TokenKind::KwConst,
          TokenKind::KwContinue,   TokenKind::KwDomain,
          TokenKind::KwEffect,     TokenKind::KwElse,
          TokenKind::KwEnsures,    TokenKind::KwExists,
          TokenKind::KwExternal,   TokenKind::KwFalse,
          TokenKind::KwFn,         TokenKind::KwFor,
          TokenKind::KwForall,     TokenKind::KwIf,
          TokenKind::KwImpl,       TokenKind::KwImport,
          TokenKind::KwIn,         TokenKind::KwInvariant,
          TokenKind::KwLaw,        TokenKind::KwLet,
          TokenKind::KwMatch,      TokenKind::KwModule,
          TokenKind::KwMut,        TokenKind::KwMutate,
          TokenKind::KwOld,        TokenKind::KwParallel,
          TokenKind::KwPub,        TokenKind::KwRequires,
          TokenKind::KwResult,     TokenKind::KwReturn,
          TokenKind::KwSelf,       TokenKind::KwTotal,
          TokenKind::KwTrait,      TokenKind::KwTransaction,
          TokenKind::KwTrue,       TokenKind::KwTrusted,
          TokenKind::KwType,       TokenKind::KwUnsafe,
          TokenKind::KwUses,       TokenKind::KwVar,
          TokenKind::KwWhere,      TokenKind::KwWhile,
          TokenKind::KwWith,       TokenKind::KwKindType,
          TokenKind::PlusEqual,    TokenKind::MinusEqual,
          TokenKind::StarEqual,    TokenKind::SlashEqual,
          TokenKind::PercentEqual, TokenKind::Percent,
          TokenKind::LogicalAnd,   TokenKind::LogicalOr,
          TokenKind::Equivalence,  TokenKind::DotDot,
          TokenKind::DotDotEqual,  TokenKind::Question,
          TokenKind::EndOfFile,
      });
}

} // namespace

int main() {
  lexes_keywords_identifiers_and_literals();
  lexes_module_keywords();
  lexes_operators_and_punctuation();
  tracks_locations_after_whitespace();
  reports_unexpected_characters();
  reports_unterminated_strings();
  lexes_the_complete_operator_and_keyword_surface();

  return EXIT_SUCCESS;
}

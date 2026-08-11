#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "flux/parser/parser.hpp"

using flux::SourceLocation;
using flux::Token;
using flux::TokenKind;
using flux::parser::BumpAllocator;
using flux::parser::Expr;
using flux::parser::LiteralExpression;
using flux::parser::Parser;
using flux::parser::ParseResult;
using flux::parser::TypeName;
using flux::parser::UnaryExpression;
using flux::parser::UnionType;

namespace {

[[noreturn]] void fail(std::string_view message) {
  std::cerr << message << '\n';
  std::exit(EXIT_FAILURE);
}

Token token(TokenKind kind, std::string lexeme, std::size_t offset = 0) {
  return {.kind = kind,
          .lexeme = std::move(lexeme),
          .location = SourceLocation{
              .offset = offset, .line = 1, .column = offset + 1}};
}

void creates_literal_in_arena() {
  Parser parser({token(TokenKind::IntegerLiteral, "42"),
                 token(TokenKind::EndOfFile, "", 2)});

  Expr *expression = parser.parse_literal();
  if (expression == nullptr ||
      !std::holds_alternative<LiteralExpression>(expression->value)) {
    fail("parser did not create a literal expression");
  }

  const auto &literal = std::get<LiteralExpression>(expression->value);
  if (literal.value.lexeme != "42") {
    fail("literal expression did not retain its token");
  }
}

void creates_recursive_unary_expression_in_arena() {
  Parser parser({token(TokenKind::Minus, "-"),
                 token(TokenKind::IntegerLiteral, "7", 1),
                 token(TokenKind::EndOfFile, "", 2)});

  Expr *expression = parser.parse_expr();
  if (expression == nullptr ||
      !std::holds_alternative<UnaryExpression>(expression->value)) {
    fail("parser did not create a unary expression");
  }

  const auto &unary = std::get<UnaryExpression>(expression->value);
  if (unary.operand == nullptr ||
      !std::holds_alternative<LiteralExpression>(unary.operand->value)) {
    fail("unary operand was not retained in the arena");
  }
}

void parse_result_retains_arena_lifetime() {
  std::weak_ptr<BumpAllocator> arena;

  {
    ParseResult result = [&arena] {
      Parser parser({token(TokenKind::EndOfFile, "")});
      ParseResult parsed = parser.parse();
      arena = parsed.arena;
      return parsed;
    }();

    if (arena.expired() || result.root == nullptr) {
      fail("parse result did not retain its AST arena");
    }

    result.root->imports.clear();
  }

  if (!arena.expired()) {
    fail("AST arena outlived its parse result");
  }
}

void parses_union_type() {
  Parser parser({token(TokenKind::Identifier, "NotFound"),
                 token(TokenKind::Pipe, "|", 9),
                 token(TokenKind::Identifier, "Forbidden", 10),
                 token(TokenKind::Pipe, "|", 19),
                 token(TokenKind::Identifier, "DatabaseError", 20),
                 token(TokenKind::EndOfFile, "", 33)});

  auto *type = parser.parse_union_type();
  if (type == nullptr || !std::holds_alternative<UnionType>(type->value)) {
    fail("parser did not create a union type");
  }

  const auto &members = std::get<UnionType>(type->value).members;
  if (members.size() != 3) {
    fail("union type did not retain all members");
  }

  for (const auto *member : members) {
    if (member == nullptr || !std::holds_alternative<TypeName>(member->value)) {
      fail("union member was not parsed as a type name");
    }
  }
}

} // namespace

int main() {
  creates_literal_in_arena();
  creates_recursive_unary_expression_in_arena();
  parse_result_retains_arena_lifetime();
  parses_union_type();
  return EXIT_SUCCESS;
}

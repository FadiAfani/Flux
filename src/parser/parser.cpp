#include "../../include/flux/parser/parser.hpp"
#include <cstddef>
#include <stdexcept>

namespace flux::parser {

Parser::Parser(const std::vector<Token> tokens) { tokens_ = std::move(tokens); }

inline void Parser::advance() { cursor_++; }

inline const Token &Parser::peek() { return tokens_[cursor_]; }

const Token &Parser::peek_next() {
  if (cursor_ >= tokens_.size() - 1) {
    return tokens_[tokens_.size() - 1]; // EOF
  }
  return tokens_[cursor_ + 1];
}

LiteralExpr Parser::parse_literal() {

  auto t = peek();

  switch (t.kind) {
  case flux::TokenKind::IntegerLiteral:
  case flux::TokenKind::FloatLiteral:
  case flux::TokenKind::StringLiteral:
    advance();
    return {.value = std::move(t)};
  default:
    throw std::out_of_range("this should not happen");
  }
}

UnaryExpr Parser::parse_unary_expr() {
  auto op = peek();
  UnaryExpr res;

  switch (op.kind) {
  case flux::TokenKind::Minus:
  case flux::TokenKind::Bang:
    res.op = std::move(op);
    advance();
    break;

  default:
    throw std::out_of_range("not a valid unary operation");
  }

  auto expr = parse_expr();

  if (expr == NULL) {
    throw std::out_of_range("expected an expression");
  }

  return std::move(res);
}

} // namespace flux::parser

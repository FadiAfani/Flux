#include "../../include/flux/parser/parser.hpp"
#include <algorithm>
#include <cstddef>
#include <memory>
#include <stdexcept>

namespace flux::parser {

Parser::Parser(const std::vector<Token> tokens) { tokens_ = std::move(tokens); }

inline void Parser::advance() { cursor_++; }

inline const Token &Parser::peek() { return tokens_[cursor_]; }

inline void Parser::report_error(SourceSpan span, std::string msg) {
    ParseError err = {.message = std::move(msg), .loc = span};
    errors_.push_back(std::move(err));
}

void Parser::report_error_with_span(const Token& start, const Token& end, std::string msg) {
    SourceSpan span = {
        .start = start.location,
        .end = {.line = end.location.line, .column = end.location.column + end.lexeme.length()}, // line could be a problem..
    };

    report_error(std::move(span), std::move(msg));
}

void Parser::report_error(const Token& start, std::string msg) {

    SourceSpan span {
        .start = start.location,
        .end = {.line = start.location.line, .column = start.location.column + start.lexeme.length()} // again not sure about line..
    };

    report_error(std::move(span), std::move(msg));
}

const Token &Parser::peek_next() {
  if (cursor_ >= tokens_.size() - 1) {
    return tokens_[tokens_.size() - 1]; // EOF
  }
  return tokens_[cursor_ + 1];
}

std::unique_ptr<LiteralExpr> Parser::parse_literal() {
  auto t = peek();

  switch (t.kind) {
  case flux::TokenKind::IntegerLiteral:
  case flux::TokenKind::FloatLiteral:
  case flux::TokenKind::StringLiteral:
    advance();
    return std::make_unique<LiteralExpr>(LiteralExpr{.value = std::move(t)});
  default:
    report_error(t, "expected an expression literal");
    return nullptr;
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

  if (expr == nullptr) {
    throw std::out_of_range("expected an expression");
  }

  return std::move(res);
}

} // namespace flux::parser

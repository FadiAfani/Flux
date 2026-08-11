#include "flux/parser/parser.hpp"

#include <utility>

namespace flux::parser {
namespace {

SourceSpan token_span(const Token &token) {
  SourceLocation end = token.location;
  end.offset += token.lexeme.size();
  end.column += token.lexeme.size();
  return {.start = token.location, .end = end};
}

template <typename Value>
Expr *make_expression(BumpAllocator &arena, SourceSpan span, Value value) {
  auto *expression = arena.create<Expr>();
  expression->span = span;
  expression->value = std::move(value);
  return expression;
}

} // namespace

Parser::Parser(std::vector<Token> tokens)
    : tokens_(std::move(tokens)), arena_(std::make_shared<BumpAllocator>()) {
  if (tokens_.empty()) {
    tokens_.push_back(Token{.kind = TokenKind::EndOfFile});
  }
}

void Parser::advance() {
  if (cursor_ + 1 < tokens_.size()) {
    ++cursor_;
  }
}

const Token &Parser::peek() { return tokens_[cursor_]; }

const Token &Parser::peek_next() {
  if (cursor_ + 1 >= tokens_.size()) {
    return tokens_.back();
  }
  return tokens_[cursor_ + 1];
}

SourceSpan Parser::get_node_span() { return token_span(peek()); }

void Parser::report_error(SourceSpan span, std::string message) {
  errors_.push_back(
      ParseError{.message = std::move(message), .loc = std::move(span)});
}

void Parser::report_error_with_span(const Token &start, const Token &end,
                                    std::string message) {
  SourceSpan span = {.start = start.location, .end = token_span(end).end};
  report_error(std::move(span), std::move(message));
}

void Parser::report_error(const Token &start, std::string message) {
  report_error(token_span(start), std::move(message));
}

ParseResult Parser::parse() {
  auto *root = arena_->create<Program>();
  root->span = token_span(tokens_.front());
  if (tokens_.size() > 1) {
    root->span.end = token_span(tokens_.back()).end;
  }

  return {.arena = arena_, .root = root, .errors = std::move(errors_)};
}

Expr *Parser::parse_literal() {
  const Token &token = peek();

  switch (token.kind) {
  case TokenKind::KwFalse:
  case TokenKind::KwTrue:
  case TokenKind::IntegerLiteral:
  case TokenKind::FloatLiteral:
  case TokenKind::StringLiteral: {
    auto *expression = make_expression(*arena_, token_span(token),
                                       LiteralExpression{.value = token});
    advance();
    return expression;
  }
  default:
    report_error(token, "expected an expression literal");
    return nullptr;
  }
}

Expr *Parser::parse_unary_expr() {
  const Token &op = peek();
  switch (op.kind) {
  case TokenKind::Bang:
  case TokenKind::Minus:
  case TokenKind::Plus:
    break;
  default:
    return nullptr;
  }

  advance();
  Expr *operand = parse_expr();
  if (operand == nullptr) {
    return nullptr;
  }

  SourceSpan span = {.start = op.location, .end = operand->span.end};
  return make_expression(*arena_, span,
                         UnaryExpression{.op = op, .operand = operand});
}

Expr *Parser::parse_expr() {
  if (Expr *unary = parse_unary_expr()) {
    return unary;
  }
  return parse_literal();
}

} // namespace flux::parser

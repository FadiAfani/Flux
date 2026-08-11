#include "flux/parser/parser.hpp"
#include "flux/lexer/token.hpp"
#include "flux/parser/allocator.hpp"
#include "flux/parser/node.hpp"

#include <optional>
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

template <typename Value>
TypePtr make_type_expression(BumpAllocator &arena, SourceSpan span,
                             Value value) {
  auto *type = arena.create<TypeExpression>();
  type->span = span;
  type->value = std::move(value);
  return type;
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

Program* Parser::parse_source_file() {
    Program* prog = arena_->create<Program>();
    prog->module = parse_module_declaration();

    while (peek().kind == TokenKind::Import) {
        auto import = parse_import_declaration();
        prog->imports.push_back(import);
    }

    while (peek().kind != TokenKind::EndOfFile) {
        std::optional<TopLevelDeclaration> tlDecl = parse_top_level_declaration();
        if (tlDecl.has_value()) {
            prog->declarations.push_back(tlDecl.value());
        }
    }

    return prog;

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

TypePtr Parser::parse_type_expression() { return parse_union_type(); }

TypePtr Parser::parse_union_type() {
  TypePtr first = parse_function_type();
  if (first == nullptr) {
    return nullptr;
  }

  std::vector<TypePtr> members = {first};
  while (peek().kind == TokenKind::Pipe) {
    advance();

    TypePtr member = parse_function_type();
    if (member == nullptr) {
      return nullptr;
    }
    members.push_back(member);
  }

  if (members.size() == 1) {
    return first;
  }

  SourceSpan span = {.start = first->span.start,
                     .end = members.back()->span.end};
  return make_type_expression(*arena_, span,
                              UnionType{.members = std::move(members)});
}

TypePtr Parser::parse_function_type() {
  if (peek().kind != TokenKind::Identifier) {
    report_error(peek(), "expected a type");
    return nullptr;
  }

  const Token &first = peek();
  QualifiedName name;
  name.parts.push_back(first);
  advance();

  while (peek().kind == TokenKind::Dot &&
         peek_next().kind == TokenKind::Identifier) {
    advance();
    name.parts.push_back(peek());
    advance();
  }

  SourceSpan span = {.start = first.location,
                     .end = token_span(name.parts.back()).end};
  return make_type_expression(*arena_, span, TypeName{.name = std::move(name)});
}


} // namespace flux::parser

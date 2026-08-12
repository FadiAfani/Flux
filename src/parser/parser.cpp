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
  Program *root = parse_source_file();

  return {.arena = arena_, .root = root, .errors = std::move(errors_)};
}

Program *Parser::parse_source_file() {
  Program *prog = arena_->create<Program>();
  prog->module = parse_module_declaration();

  while (peek().kind == TokenKind::KwImport) {
    auto import = parse_import_declaration();
    if (import != nullptr) {
      prog->imports.push_back(import);
    }
  }

  while (peek().kind != TokenKind::EndOfFile) {
    const std::size_t start = cursor_;
    std::optional<TopLevelDeclaration> tlDecl = parse_top_level_declaration();
    if (tlDecl.has_value()) {
      prog->declarations.push_back(tlDecl.value());
    }
    if (cursor_ == start) {
      advance();
    }
  }

  prog->span = {.start = tokens_.front().location,
                .end = token_span(tokens_.back()).end};

  return prog;
}

QualifiedName Parser::parse_qualified_name() {
  QualifiedName name;
  if (peek().kind != TokenKind::Identifier) {
    report_error(peek(), "expected an identifier");
    return name;
  }

  name.parts.push_back(peek());
  advance();

  while (peek().kind == TokenKind::Dot) {
    advance();
    if (peek().kind != TokenKind::Identifier) {
      report_error(peek(), "expected an identifier after '.'");
      return {};
    }

    name.parts.push_back(peek());
    advance();
  }

  return name;
}

ModuleDeclaration *Parser::parse_module_declaration() {
  if (peek().kind != TokenKind::KwModule) {
    return nullptr;
  }

  const Token start = peek();
  advance();

  QualifiedName name = parse_qualified_name();
  if (name.parts.empty()) {
    return nullptr;
  }

  if (peek().kind != TokenKind::SemiColon) {
    report_error(peek(), "expected ';' after module declaration");
    return nullptr;
  }

  const Token end = peek();
  advance();

  auto *declaration = arena_->create<ModuleDeclaration>();
  declaration->span = {.start = start.location, .end = token_span(end).end};
  declaration->name = std::move(name);
  return declaration;
}

ImportDeclaration *Parser::parse_import_declaration() {
  if (peek().kind != TokenKind::KwImport) {
    return nullptr;
  }

  const Token start = peek();
  advance();

  QualifiedName name = parse_qualified_name();
  if (name.parts.empty()) {
    return nullptr;
  }

  if (peek().kind != TokenKind::SemiColon) {
    report_error(peek(), "expected ';' after import declaration");
    return nullptr;
  }

  const Token end = peek();
  advance();

  auto *declaration = arena_->create<ImportDeclaration>();
  declaration->span = {.start = start.location, .end = token_span(end).end};
  declaration->name = std::move(name);
  return declaration;
}

std::optional<TopLevelDeclaration> Parser::parse_top_level_declaration() {
  std::size_t index = cursor_;
  if (tokens_[index].kind == TokenKind::KwPub) {
    ++index;
  }

  if (index < tokens_.size() &&
      (tokens_[index].kind == TokenKind::KwTrusted ||
       tokens_[index].kind == TokenKind::KwExternal)) {
    if (auto *declaration = parse_external_function_declaration()) {
      return TopLevelDeclaration{declaration};
    }
    return std::nullopt;
  }

  if (index < tokens_.size() && (tokens_[index].kind == TokenKind::KwTotal ||
                                 tokens_[index].kind == TokenKind::KwFn)) {
    if (auto *declaration = parse_function_declaration()) {
      return TopLevelDeclaration{declaration};
    }
    return std::nullopt;
  }

  report_error(peek(), "expected a top-level declaration");
  return std::nullopt;
}

std::optional<FunctionSignature> Parser::parse_function_signature() {
  if (peek().kind != TokenKind::KwFn) {
    report_error(peek(), "expected 'fn'");
    return std::nullopt;
  }
  advance();

  if (peek().kind != TokenKind::Identifier) {
    report_error(peek(), "expected function name");
    return std::nullopt;
  }

  FunctionSignature signature;
  signature.name = peek();
  advance();

  if (peek().kind == TokenKind::Less) {
    advance();
    while (true) {
      if (peek().kind != TokenKind::Identifier) {
        report_error(peek(), "expected generic parameter name");
        return std::nullopt;
      }

      GenericParameter parameter;
      parameter.name = peek();
      advance();

      if (peek().kind == TokenKind::Colon) {
        advance();
        TypePtr domain = parse_type_expression();
        if (domain == nullptr) {
          return std::nullopt;
        }
        parameter.domain = domain;
      }

      signature.generic_parameters.push_back(std::move(parameter));
      if (peek().kind != TokenKind::Comma) {
        break;
      }
      advance();
    }

    if (peek().kind != TokenKind::Greater) {
      report_error(peek(), "expected '>' after generic parameters");
      return std::nullopt;
    }
    advance();
  }

  if (peek().kind != TokenKind::LParen) {
    report_error(peek(), "expected '(' after function name");
    return std::nullopt;
  }
  advance();

  if (peek().kind != TokenKind::RParen) {
    while (true) {
      if (peek().kind != TokenKind::Identifier) {
        report_error(peek(), "expected parameter name");
        return std::nullopt;
      }

      Parameter parameter;
      parameter.name = peek();
      advance();

      if (peek().kind != TokenKind::Colon) {
        report_error(peek(), "expected ':' after parameter name");
        return std::nullopt;
      }
      advance();

      parameter.is_mutable = peek().kind == TokenKind::KwMut;
      if (parameter.is_mutable) {
        advance();
      }

      parameter.type = parse_type_expression();
      if (parameter.type == nullptr) {
        return std::nullopt;
      }
      signature.parameters.push_back(std::move(parameter));

      if (peek().kind != TokenKind::Comma) {
        break;
      }
      advance();
    }
  }

  if (peek().kind != TokenKind::RParen) {
    report_error(peek(), "expected ')' after parameters");
    return std::nullopt;
  }
  advance();

  if (peek().kind == TokenKind::Arrow) {
    advance();
    signature.return_type = parse_type_expression();
    if (signature.return_type == nullptr) {
      return std::nullopt;
    }
  }

  if (peek().kind == TokenKind::KwRequires ||
      peek().kind == TokenKind::KwEnsures || peek().kind == TokenKind::KwUses) {
    report_error(peek(), "function clauses are not implemented");
    return std::nullopt;
  }

  return signature;
}

bool Parser::parse_visibility() {
  if (peek().kind != TokenKind::KwPub) {
    return false;
  }
  advance();
  return true;
}

FunctionDeclaration *Parser::parse_function_declaration() {
  const Token start = peek();
  const bool visible = parse_visibility();

  const bool total = peek().kind == TokenKind::KwTotal;
  if (total) {
    advance();
  }

  auto signature = parse_function_signature();
  if (!signature.has_value()) {
    return nullptr;
  }

  BlockPtr body = parse_block();
  if (body == nullptr) {
    return nullptr;
  }

  auto *declaration = arena_->create<FunctionDeclaration>();
  declaration->span = {.start = start.location, .end = body->span.end};
  declaration->is_public = visible;
  declaration->is_total = total;
  declaration->signature = std::move(signature.value());
  declaration->body = body;
  return declaration;
}

ExternalFunctionDeclaration *Parser::parse_external_function_declaration() {
  const Token start = peek();
  const bool visible = parse_visibility();

  const bool trusted = peek().kind == TokenKind::KwTrusted;
  if (trusted) {
    advance();
  }

  if (peek().kind != TokenKind::KwExternal) {
    report_error(peek(), "expected 'external'");
    return nullptr;
  }
  advance();

  auto signature = parse_function_signature();
  if (!signature.has_value()) {
    return nullptr;
  }

  if (peek().kind != TokenKind::SemiColon) {
    report_error(peek(), "expected ';' after external function declaration");
    return nullptr;
  }

  const Token end = peek();
  advance();

  auto *declaration = arena_->create<ExternalFunctionDeclaration>();
  declaration->span = {.start = start.location, .end = token_span(end).end};
  declaration->is_public = visible;
  declaration->is_trusted = trusted;
  declaration->signature = std::move(signature.value());
  return declaration;
}

std::optional<TypeDeclaration> Parser::parse_type_declaration() {
  std::size_t index = cursor_;
  if (tokens_[index].kind == TokenKind::KwPub) {
    ++index;
  }

  if (index >= tokens_.size() || tokens_[index].kind != TokenKind::KwType) {
    report_error(peek(), "expected 'type'");
    return std::nullopt;
  }
  ++index;

  if (index >= tokens_.size() || tokens_[index].kind != TokenKind::Identifier) {
    report_error(peek(), "expected type name");
    return std::nullopt;
  }
  ++index;

  if (index < tokens_.size() && tokens_[index].kind == TokenKind::Less) {
    std::size_t depth = 0;
    do {
      if (tokens_[index].kind == TokenKind::Less) {
        ++depth;
      } else if (tokens_[index].kind == TokenKind::Greater) {
        --depth;
      }
      ++index;
    } while (index < tokens_.size() && depth != 0);

    if (depth != 0) {
      report_error(peek(), "expected '>' after generic parameters");
      return std::nullopt;
    }
  }

  if (index < tokens_.size() && tokens_[index].kind == TokenKind::Equal) {
    auto *alias = parse_type_alias_declaration();
    if (alias == nullptr) {
      return std::nullopt;
    }
    return TypeDeclaration{alias};
  }

  if (index < tokens_.size() && tokens_[index].kind == TokenKind::LBrace) {
    report_error(tokens_[index],
                 "record type declarations are not implemented");
    return std::nullopt;
  }

  report_error(index < tokens_.size() ? tokens_[index] : tokens_.back(),
               "expected '=' or '{' after type name");
  return std::nullopt;
}

TypeAliasDeclaration *Parser::parse_type_alias_declaration() {
  const Token start = peek();
  const bool visible = parse_visibility();

  if (peek().kind != TokenKind::KwType) {
    report_error(peek(), "expected 'type'");
    return nullptr;
  }
  advance();

  if (peek().kind != TokenKind::Identifier) {
    report_error(peek(), "expected type name");
    return nullptr;
  }
  const Token name = peek();
  advance();

  std::vector<GenericParameter> generic_parameters;
  if (peek().kind == TokenKind::Less) {
    advance();

    auto first = parse_generic_parameter();
    if (!first.has_value()) {
      return nullptr;
    }
    generic_parameters.push_back(std::move(first.value()));

    while (peek().kind == TokenKind::Comma) {
      advance();
      auto parameter = parse_generic_parameter();
      if (!parameter.has_value()) {
        return nullptr;
      }
      generic_parameters.push_back(std::move(parameter.value()));
    }

    if (peek().kind != TokenKind::Greater) {
      report_error(peek(), "expected '>' after generic parameters");
      return nullptr;
    }
    advance();
  }

  if (peek().kind != TokenKind::Equal) {
    report_error(peek(), "expected '=' in type alias");
    return nullptr;
  }
  advance();

  TypePtr type = parse_type_expression();
  if (type == nullptr) {
    return nullptr;
  }

  if (peek().kind != TokenKind::SemiColon) {
    report_error(peek(), "expected ';' after type alias");
    return nullptr;
  }
  const Token end = peek();
  advance();

  auto *declaration = arena_->create<TypeAliasDeclaration>();
  declaration->span = {.start = start.location, .end = token_span(end).end};
  declaration->is_public = visible;
  declaration->name = name;
  declaration->generic_parameters = std::move(generic_parameters);
  declaration->type = type;
  return declaration;
}

std::optional<GenericParameter> Parser::parse_generic_parameter() {
  if (peek().kind != TokenKind::Identifier) {
    report_error(peek(), "expected generic parameter name");
    return std::nullopt;
  }

  GenericParameter parameter;
  parameter.name = peek();
  advance();

  if (peek().kind == TokenKind::Colon) {
    advance();
    TypePtr domain = parse_type_expression();
    if (domain == nullptr) {
      return std::nullopt;
    }
    parameter.domain = domain;
  }

  return parameter;
}

BlockPtr Parser::parse_block() {
  if (peek().kind != TokenKind::LBrace) {
    report_error(peek(), "expected function body");
    return nullptr;
  }

  const Token start = peek();
  advance();

  ExprPtr tail = nullptr;
  if (peek().kind != TokenKind::RBrace) {
    tail = parse_expr();
    if (tail == nullptr) {
      return nullptr;
    }
  }

  if (peek().kind != TokenKind::RBrace) {
    report_error(peek(), "expected '}' after function body");
    return nullptr;
  }

  const Token end = peek();
  advance();

  auto *block = arena_->create<Block>();
  block->span = {.start = start.location, .end = token_span(end).end};
  block->tail_expression = tail;
  return block;
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

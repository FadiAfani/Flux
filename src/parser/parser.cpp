#include "flux/parser/parser.hpp"

#include <algorithm>
#include <cctype>
#include <utility>

namespace flux::parser {
namespace {

SourceLocation token_end(const Token &token) {
  SourceLocation end = token.location;
  const std::size_t delimiters =
      token.kind == TokenKind::StringLiteral ||
              token.kind == TokenKind::CharacterLiteral
          ? 2
          : 0;
  end.offset += token.lexeme.size() + delimiters;
  end.column += token.lexeme.size() + delimiters;
  return end;
}

SourceSpan token_span(const Token &token) {
  return {.start = token.location, .end = token_end(token)};
}

template <typename NodeType, typename Value>
NodeType *make_node(BumpAllocator &arena, SourceSpan span, Value value) {
  auto *node = arena.create<NodeType>();
  node->span = span;
  node->value = std::move(value);
  return node;
}

bool is_declaration_start(TokenKind kind) {
  switch (kind) {
  case TokenKind::KwPub:
  case TokenKind::KwTotal:
  case TokenKind::KwTrusted:
  case TokenKind::KwExternal:
  case TokenKind::KwFn:
  case TokenKind::KwType:
  case TokenKind::KwTrait:
  case TokenKind::KwImpl:
  case TokenKind::KwConst:
  case TokenKind::KwEffect:
  case TokenKind::KwDomain:
    return true;
  default:
    return false;
  }
}

bool is_assignment(TokenKind kind) {
  switch (kind) {
  case TokenKind::Equal:
  case TokenKind::PlusEqual:
  case TokenKind::MinusEqual:
  case TokenKind::StarEqual:
  case TokenKind::SlashEqual:
  case TokenKind::PercentEqual:
    return true;
  default:
    return false;
  }
}

} // namespace

Parser::Parser(std::vector<Token> tokens)
    : tokens_(std::move(tokens)), arena_(std::make_shared<BumpAllocator>()) {
  if (tokens_.empty() || tokens_.back().kind != TokenKind::EndOfFile) {
    SourceLocation location{};
    if (!tokens_.empty()) {
      location = token_end(tokens_.back());
    }
    tokens_.push_back(
        Token{.kind = TokenKind::EndOfFile, .location = location});
  }
}

const Token &Parser::current(std::size_t lookahead) const {
  return tokens_[std::min(cursor_ + lookahead, tokens_.size() - 1)];
}

const Token &Parser::previous() const {
  return tokens_[cursor_ == 0 ? 0 : cursor_ - 1];
}

bool Parser::is_at_end() const {
  return current().kind == TokenKind::EndOfFile;
}

bool Parser::check(TokenKind kind, std::size_t lookahead) const {
  return current(lookahead).kind == kind;
}

bool Parser::match(TokenKind kind) {
  if (!check(kind)) {
    return false;
  }
  advance();
  return true;
}

bool Parser::expect(TokenKind kind, std::string message) {
  if (match(kind)) {
    return true;
  }
  report_error(current(), std::move(message));
  return false;
}

Token Parser::take(TokenKind kind, std::string message) {
  if (check(kind)) {
    Token token = current();
    advance();
    return token;
  }
  report_error(current(), std::move(message));
  return Token{.kind = TokenKind::Invalid, .location = current().location};
}

SourceSpan Parser::span_from(const Token &start) const {
  return {.start = start.location, .end = token_end(previous())};
}

void Parser::advance() {
  if (!is_at_end()) {
    ++cursor_;
  }
}

const Token &Parser::peek() { return current(); }
const Token &Parser::peek_next() { return current(1); }
SourceSpan Parser::get_node_span() { return token_span(current()); }

void Parser::report_error(SourceSpan span, std::string message) {
  errors_.push_back({.message = std::move(message), .loc = span});
}

void Parser::report_error_with_span(const Token &start, const Token &end,
                                    std::string message) {
  report_error({.start = start.location, .end = token_end(end)},
               std::move(message));
}

void Parser::report_error(const Token &start, std::string message) {
  report_error(token_span(start), std::move(message));
}

void Parser::synchronize_declaration() {
  while (!is_at_end()) {
    if (match(TokenKind::SemiColon)) {
      return;
    }
    if (is_declaration_start(current().kind)) {
      return;
    }
    advance();
  }
}

void Parser::synchronize_statement() {
  while (!is_at_end() && !check(TokenKind::RBrace)) {
    if (match(TokenKind::SemiColon)) {
      return;
    }
    advance();
  }
}

bool Parser::looks_like_assignment() const {
  if (!check(TokenKind::Identifier)) {
    return false;
  }
  std::size_t index = cursor_ + 1;
  while (index < tokens_.size()) {
    if (tokens_[index].kind == TokenKind::Dot && index + 1 < tokens_.size() &&
        tokens_[index + 1].kind == TokenKind::Identifier) {
      index += 2;
      continue;
    }
    if (tokens_[index].kind == TokenKind::LBracket) {
      std::size_t depth = 1;
      ++index;
      while (index < tokens_.size() && depth != 0) {
        if (tokens_[index].kind == TokenKind::LBracket) {
          ++depth;
        } else if (tokens_[index].kind == TokenKind::RBracket) {
          --depth;
        }
        ++index;
      }
      if (depth != 0) {
        return false;
      }
      continue;
    }
    break;
  }
  return index < tokens_.size() && is_assignment(tokens_[index].kind);
}

bool Parser::looks_like_kind_expression() const {
  std::size_t index = cursor_;
  while (index < tokens_.size() && tokens_[index].kind == TokenKind::LParen) {
    ++index;
  }
  return index < tokens_.size() && tokens_[index].kind == TokenKind::KwKindType;
}

ParseResult Parser::parse() {
  Program *root = parse_source_file();
  return {.arena = arena_, .root = root, .errors = std::move(errors_)};
}

Program *Parser::parse_source_file() {
  auto *source = arena_->create<Program>();
  const Token start = current();
  source->module = parse_module_declaration();

  while (check(TokenKind::KwImport)) {
    const std::size_t before = cursor_;
    if (auto *declaration = parse_import_declaration()) {
      source->imports.push_back(declaration);
    } else {
      synchronize_declaration();
    }
    if (cursor_ == before) {
      advance();
    }
  }

  while (!is_at_end()) {
    const std::size_t before = cursor_;
    if (auto declaration = parse_top_level_declaration()) {
      source->declarations.push_back(std::move(*declaration));
    } else {
      synchronize_declaration();
    }
    if (cursor_ == before) {
      advance();
    }
  }

  source->span = {.start = start.location, .end = token_end(current())};
  return source;
}

QualifiedName Parser::parse_qualified_name() {
  QualifiedName name;
  Token part = take(TokenKind::Identifier, "expected an identifier");
  if (part.kind == TokenKind::Invalid) {
    return name;
  }
  name.parts.push_back(std::move(part));
  while (check(TokenKind::Dot) && check(TokenKind::Identifier, 1)) {
    advance();
    name.parts.push_back(current());
    advance();
  }
  return name;
}

ModuleDeclaration *Parser::parse_module_declaration() {
  if (!check(TokenKind::KwModule)) {
    return nullptr;
  }
  const Token start = current();
  advance();
  QualifiedName name = parse_qualified_name();
  if (name.parts.empty() ||
      !expect(TokenKind::SemiColon, "expected ';' after module declaration")) {
    return nullptr;
  }
  auto *declaration = arena_->create<ModuleDeclaration>();
  declaration->span = span_from(start);
  declaration->name = std::move(name);
  return declaration;
}

ImportDeclaration *Parser::parse_import_declaration() {
  if (!check(TokenKind::KwImport)) {
    return nullptr;
  }
  const Token start = current();
  advance();
  QualifiedName name = parse_qualified_name();
  if (name.parts.empty()) {
    return nullptr;
  }
  auto selector = parse_import_selector();
  if (!expect(TokenKind::SemiColon, "expected ';' after import declaration")) {
    return nullptr;
  }
  auto *declaration = arena_->create<ImportDeclaration>();
  declaration->span = span_from(start);
  declaration->name = std::move(name);
  declaration->selector = std::move(selector);
  return declaration;
}

std::optional<ImportSelector> Parser::parse_import_selector() {
  if (match(TokenKind::KwAs)) {
    Token alias = take(TokenKind::Identifier, "expected import alias");
    if (alias.kind == TokenKind::Invalid) {
      return std::nullopt;
    }
    return ImportAlias{.alias = std::move(alias)};
  }
  if (!match(TokenKind::Dot)) {
    return std::nullopt;
  }
  if (match(TokenKind::Star)) {
    return ImportAll{};
  }
  if (!expect(TokenKind::LBrace, "expected '*' or '{' after '.'")) {
    return std::nullopt;
  }
  auto items = parse_import_item_list();
  if (!expect(TokenKind::RBrace, "expected '}' after import items")) {
    return std::nullopt;
  }
  return ImportItems{.items = std::move(items)};
}

std::vector<ImportItem> Parser::parse_import_item_list() {
  std::vector<ImportItem> items;
  if (check(TokenKind::RBrace)) {
    report_error(current(), "expected at least one import item");
    return items;
  }
  items.push_back(parse_import_item());
  while (match(TokenKind::Comma)) {
    items.push_back(parse_import_item());
  }
  return items;
}

ImportItem Parser::parse_import_item() {
  ImportItem item{.name = take(TokenKind::Identifier, "expected import name")};
  if (match(TokenKind::KwAs)) {
    item.alias = take(TokenKind::Identifier, "expected import alias");
  }
  return item;
}

bool Parser::parse_visibility() { return match(TokenKind::KwPub); }

std::optional<TopLevelDeclaration> Parser::parse_top_level_declaration() {
  std::size_t offset = check(TokenKind::KwPub) ? 1 : 0;
  TokenKind kind = current(offset).kind;
  if (kind == TokenKind::KwTrusted || kind == TokenKind::KwExternal) {
    if (auto *node = parse_external_function_declaration()) {
      return TopLevelDeclaration{node};
    }
    return std::nullopt;
  }
  if (kind == TokenKind::KwTotal || kind == TokenKind::KwFn) {
    if (auto *node = parse_function_declaration()) {
      return TopLevelDeclaration{node};
    }
    return std::nullopt;
  }
  if (kind == TokenKind::KwType) {
    auto type = parse_type_declaration();
    if (!type) {
      return std::nullopt;
    }
    return std::visit([](auto *node) -> TopLevelDeclaration { return node; },
                      *type);
  }
  if (kind == TokenKind::KwTrait) {
    if (auto *node = parse_trait_declaration())
      return TopLevelDeclaration{node};
  } else if (kind == TokenKind::KwImpl) {
    if (auto *node = parse_impl_declaration())
      return TopLevelDeclaration{node};
  } else if (kind == TokenKind::KwConst) {
    if (auto *node = parse_const_declaration())
      return TopLevelDeclaration{node};
  } else if (kind == TokenKind::KwEffect) {
    if (auto *node = parse_effect_declaration())
      return TopLevelDeclaration{node};
  } else if (kind == TokenKind::KwDomain) {
    if (auto *node = parse_domain_declaration())
      return TopLevelDeclaration{node};
  } else {
    report_error(current(), "expected a top-level declaration");
  }
  return std::nullopt;
}

std::optional<FunctionSignature> Parser::parse_function_signature() {
  if (!expect(TokenKind::KwFn, "expected 'fn'")) {
    return std::nullopt;
  }
  FunctionSignature signature;
  signature.name = take(TokenKind::Identifier, "expected function name");
  if (signature.name.kind == TokenKind::Invalid) {
    return std::nullopt;
  }
  if (check(TokenKind::Less)) {
    signature.generic_parameters = parse_generic_parameter_list();
    if (!check(TokenKind::LParen)) {
      return std::nullopt;
    }
  }
  if (!expect(TokenKind::LParen, "expected '(' after function name")) {
    return std::nullopt;
  }
  if (!check(TokenKind::RParen)) {
    signature.parameters = parse_parameter_list();
  }
  if (!expect(TokenKind::RParen, "expected ')' after parameters")) {
    return std::nullopt;
  }
  if (match(TokenKind::Arrow)) {
    signature.return_type = parse_type_expression();
    if (!signature.return_type) {
      return std::nullopt;
    }
  }
  while (check(TokenKind::KwRequires) || check(TokenKind::KwEnsures) ||
         check(TokenKind::KwUses)) {
    const std::size_t error_count = errors_.size();
    signature.clauses.push_back(parse_function_clause());
    if (errors_.size() != error_count) {
      return std::nullopt;
    }
  }
  return signature;
}

FunctionDeclaration *Parser::parse_function_declaration() {
  const Token start = current();
  const bool is_public = parse_visibility();
  const bool is_total = match(TokenKind::KwTotal);
  auto signature = parse_function_signature();
  if (!signature) {
    return nullptr;
  }
  BlockPtr body = parse_block();
  if (!body) {
    return nullptr;
  }
  auto *declaration = arena_->create<FunctionDeclaration>();
  declaration->span = {.start = start.location, .end = body->span.end};
  declaration->is_public = is_public;
  declaration->is_total = is_total;
  declaration->signature = std::move(*signature);
  declaration->body = body;
  return declaration;
}

ExternalFunctionDeclaration *Parser::parse_external_function_declaration() {
  const Token start = current();
  const bool is_public = parse_visibility();
  const bool is_trusted = match(TokenKind::KwTrusted);
  if (!expect(TokenKind::KwExternal, "expected 'external'")) {
    return nullptr;
  }
  auto signature = parse_function_signature();
  if (!signature)
    return nullptr;
  if (signature->clauses.empty()) {
    if (!expect(TokenKind::SemiColon,
                "expected ';' after external function declaration"))
      return nullptr;
  } else {
    match(TokenKind::SemiColon);
  }
  auto *declaration = arena_->create<ExternalFunctionDeclaration>();
  declaration->span = span_from(start);
  declaration->is_public = is_public;
  declaration->is_trusted = is_trusted;
  declaration->signature = std::move(*signature);
  return declaration;
}

ConstantDeclaration *Parser::parse_const_declaration() {
  const Token start = current();
  const bool is_public = parse_visibility();
  if (!expect(TokenKind::KwConst, "expected 'const'"))
    return nullptr;
  Token name = take(TokenKind::Identifier, "expected constant name");
  TypePtr type = nullptr;
  if (match(TokenKind::Colon)) {
    type = parse_type_expression();
    if (!type)
      return nullptr;
  }
  if (!expect(TokenKind::Equal, "expected '=' in constant declaration"))
    return nullptr;
  ExprPtr value = parse_expr();
  if (!value || !expect(TokenKind::SemiColon, "expected ';' after constant"))
    return nullptr;
  auto *declaration = arena_->create<ConstantDeclaration>();
  declaration->span = span_from(start);
  declaration->is_public = is_public;
  declaration->name = std::move(name);
  declaration->type = type;
  declaration->value = value;
  return declaration;
}

std::vector<Parameter> Parser::parse_parameter_list() {
  std::vector<Parameter> parameters;
  parameters.push_back(parse_parameter());
  while (match(TokenKind::Comma)) {
    if (check(TokenKind::RParen)) {
      report_error(current(),
                   "trailing comma is not allowed in parameter list");
      break;
    }
    parameters.push_back(parse_parameter());
  }
  return parameters;
}

Parameter Parser::parse_parameter() {
  Parameter parameter;
  parameter.name = take(TokenKind::Identifier, "expected parameter name");
  if (!expect(TokenKind::Colon, "expected ':' after parameter name"))
    return parameter;
  auto [is_mutable, type] = parse_parameter_type();
  parameter.is_mutable = is_mutable;
  parameter.type = type;
  return parameter;
}

std::pair<bool, TypePtr> Parser::parse_parameter_type() {
  const bool is_mutable = match(TokenKind::KwMut);
  return {is_mutable, parse_type_expression()};
}

FunctionClause Parser::parse_function_clause() {
  if (check(TokenKind::KwRequires))
    return parse_requires_clause();
  if (check(TokenKind::KwEnsures))
    return parse_ensures_clause();
  if (check(TokenKind::KwUses))
    return parse_uses_clause();
  report_error(current(), "expected function clause");
  return RequiresClause{};
}

RequiresClause Parser::parse_requires_clause() {
  expect(TokenKind::KwRequires, "expected 'requires'");
  PredicatePtr predicate = parse_predicate();
  expect(TokenKind::SemiColon, "expected ';' after requires clause");
  return {.predicate = predicate};
}

EnsuresClause Parser::parse_ensures_clause() {
  expect(TokenKind::KwEnsures, "expected 'ensures'");
  PredicatePtr predicate = parse_predicate();
  expect(TokenKind::SemiColon, "expected ';' after ensures clause");
  return {.predicate = predicate};
}

UsesClause Parser::parse_uses_clause() {
  expect(TokenKind::KwUses, "expected 'uses'");
  auto effects = parse_effect_reference_list();
  expect(TokenKind::SemiColon, "expected ';' after uses clause");
  return {.effects = std::move(effects)};
}

std::vector<EffectReference> Parser::parse_effect_reference_list() {
  std::vector<EffectReference> effects;
  effects.push_back(parse_effect_reference());
  while (match(TokenKind::Comma))
    effects.push_back(parse_effect_reference());
  return effects;
}

EffectReference Parser::parse_effect_reference() {
  EffectReference reference{.name = parse_qualified_name()};
  if (check(TokenKind::Less)) {
    reference.arguments = parse_meta_argument_list();
  }
  return reference;
}

std::vector<GenericParameter> Parser::parse_generic_parameter_list() {
  std::vector<GenericParameter> parameters;
  if (!expect(TokenKind::Less, "expected '<'"))
    return parameters;
  auto first = parse_generic_parameter();
  if (!first)
    return parameters;
  parameters.push_back(std::move(*first));
  while (match(TokenKind::Comma)) {
    auto parameter = parse_generic_parameter();
    if (!parameter)
      return {};
    parameters.push_back(std::move(*parameter));
  }
  if (!expect(TokenKind::Greater, "expected '>' after generic parameters"))
    return {};
  return parameters;
}

std::optional<GenericParameter> Parser::parse_generic_parameter() {
  Token name = take(TokenKind::Identifier, "expected generic parameter name");
  if (name.kind == TokenKind::Invalid)
    return std::nullopt;
  GenericParameter parameter{.name = std::move(name)};
  if (match(TokenKind::Colon)) {
    auto domain = parse_generic_domain();
    if (std::holds_alternative<KindPtr>(domain)) {
      KindPtr kind = std::get<KindPtr>(domain);
      if (!kind)
        return std::nullopt;
      parameter.domain = kind;
    } else {
      TypePtr type = std::get<TypePtr>(domain);
      if (!type)
        return std::nullopt;
      parameter.domain = type;
    }
  }
  return parameter;
}

std::variant<KindPtr, TypePtr> Parser::parse_generic_domain() {
  if (looks_like_kind_expression())
    return parse_kind_expression();
  return parse_type_expression();
}

KindPtr Parser::parse_kind_expression() {
  return parse_kind_arrow_expression();
}

KindPtr Parser::parse_kind_arrow_expression() {
  KindPtr left = parse_kind_primary();
  if (!left)
    return nullptr;
  if (!match(TokenKind::Arrow))
    return left;
  KindPtr right = parse_kind_arrow_expression();
  if (!right)
    return nullptr;
  return make_node<KindExpression>(
      *arena_, {.start = left->span.start, .end = right->span.end},
      ArrowKind{.parameter = left, .result = right});
}

KindPtr Parser::parse_kind_primary() {
  const Token start = current();
  if (match(TokenKind::KwKindType)) {
    return make_node<KindExpression>(*arena_, span_from(start), TypeKind{});
  }
  if (match(TokenKind::LParen)) {
    KindPtr kind = parse_kind_expression();
    if (!kind || !expect(TokenKind::RParen, "expected ')' after kind"))
      return nullptr;
    return make_node<KindExpression>(*arena_, span_from(start),
                                     ParenthesizedKind{.kind = kind});
  }
  report_error(current(), "expected a kind expression");
  return nullptr;
}

std::vector<MetaArgument> Parser::parse_meta_argument_list() {
  std::vector<MetaArgument> arguments;
  if (!expect(TokenKind::Less, "expected '<'"))
    return arguments;
  arguments.push_back(parse_meta_argument());
  while (match(TokenKind::Comma))
    arguments.push_back(parse_meta_argument());
  expect(TokenKind::Greater, "expected '>' after meta arguments");
  return arguments;
}

MetaArgument Parser::parse_meta_argument() {
  switch (current().kind) {
  case TokenKind::IntegerLiteral:
  case TokenKind::FloatLiteral:
  case TokenKind::CharacterLiteral:
  case TokenKind::StringLiteral:
  case TokenKind::KwTrue:
  case TokenKind::KwFalse:
  case TokenKind::Plus:
  case TokenKind::Minus:
    return {.value = parse_constant_meta_expression()};
  default: {
    const std::size_t saved_cursor = cursor_;
    const std::size_t saved_errors = errors_.size();
    TypePtr type = parse_type_expression();
    if (type && (check(TokenKind::Comma) || check(TokenKind::Greater))) {
      return {.value = type};
    }
    cursor_ = saved_cursor;
    errors_.resize(saved_errors);
    return {.value = parse_constant_meta_expression()};
  }
  }
}

Expr *Parser::parse_constant_meta_expression() {
  return parse_additive_expression();
}

std::optional<TypeDeclaration> Parser::parse_type_declaration() {
  std::size_t index = cursor_ + (check(TokenKind::KwPub) ? 1 : 0);
  if (index >= tokens_.size() || tokens_[index].kind != TokenKind::KwType) {
    report_error(current(), "expected 'type'");
    return std::nullopt;
  }
  ++index;
  if (index >= tokens_.size() || tokens_[index].kind != TokenKind::Identifier) {
    report_error(tokens_[std::min(index, tokens_.size() - 1)],
                 "expected type name");
    return std::nullopt;
  }
  ++index;
  if (index < tokens_.size() && tokens_[index].kind == TokenKind::Less) {
    std::size_t depth = 0;
    do {
      if (tokens_[index].kind == TokenKind::Less)
        ++depth;
      if (tokens_[index].kind == TokenKind::Greater && depth != 0)
        --depth;
      ++index;
    } while (index < tokens_.size() && depth != 0);
    if (depth != 0) {
      report_error(current(), "unterminated generic parameter list");
      return std::nullopt;
    }
  }
  if (index < tokens_.size() && tokens_[index].kind == TokenKind::LBrace) {
    if (auto *node = parse_record_type_declaration())
      return TypeDeclaration{node};
    return std::nullopt;
  }
  if (index >= tokens_.size() || tokens_[index].kind != TokenKind::Equal) {
    report_error(tokens_[std::min(index, tokens_.size() - 1)],
                 "expected '=' or '{' after type name");
    return std::nullopt;
  }

  std::size_t nesting = 0;
  bool has_top_level_pipe = false;
  for (++index;
       index < tokens_.size() && tokens_[index].kind != TokenKind::EndOfFile;
       ++index) {
    const TokenKind kind = tokens_[index].kind;
    if (kind == TokenKind::LParen || kind == TokenKind::LBracket ||
        kind == TokenKind::LBrace || kind == TokenKind::Less)
      ++nesting;
    if ((kind == TokenKind::RParen || kind == TokenKind::RBracket ||
         kind == TokenKind::RBrace || kind == TokenKind::Greater) &&
        nesting != 0)
      --nesting;
    if (kind == TokenKind::Pipe && nesting == 0)
      has_top_level_pipe = true;
    if (kind == TokenKind::SemiColon && nesting == 0)
      break;
  }
  if (has_top_level_pipe) {
    if (auto *node = parse_sum_type_declaration())
      return TypeDeclaration{node};
  } else if (auto *node = parse_type_alias_declaration()) {
    return TypeDeclaration{node};
  }
  return std::nullopt;
}

TypeAliasDeclaration *Parser::parse_type_alias_declaration() {
  const Token start = current();
  const bool is_public = parse_visibility();
  if (!expect(TokenKind::KwType, "expected 'type'"))
    return nullptr;
  Token name = take(TokenKind::Identifier, "expected type name");
  std::vector<GenericParameter> parameters;
  if (check(TokenKind::Less)) {
    parameters = parse_generic_parameter_list();
    if (!check(TokenKind::Equal))
      return nullptr;
  }
  if (!expect(TokenKind::Equal, "expected '=' in type alias"))
    return nullptr;
  TypePtr type = parse_type_expression();
  if (!type)
    return nullptr;
  PredicatePtr refinement = parse_refinement_clause();
  if (!expect(TokenKind::SemiColon, "expected ';' after type alias"))
    return nullptr;
  auto *declaration = arena_->create<TypeAliasDeclaration>();
  declaration->span = span_from(start);
  declaration->is_public = is_public;
  declaration->name = std::move(name);
  declaration->generic_parameters = std::move(parameters);
  declaration->type = type;
  declaration->refinement = refinement;
  return declaration;
}

PredicatePtr Parser::parse_refinement_clause() {
  if (!match(TokenKind::KwWhere))
    return nullptr;
  return parse_predicate();
}

RecordTypeDeclaration *Parser::parse_record_type_declaration() {
  const Token start = current();
  const bool is_public = parse_visibility();
  if (!expect(TokenKind::KwType, "expected 'type'"))
    return nullptr;
  Token name = take(TokenKind::Identifier, "expected record type name");
  std::vector<GenericParameter> parameters;
  if (check(TokenKind::Less))
    parameters = parse_generic_parameter_list();
  if (!expect(TokenKind::LBrace, "expected '{' in record declaration"))
    return nullptr;
  std::vector<RecordMember> members;
  while (!check(TokenKind::RBrace) && !is_at_end()) {
    const std::size_t before = cursor_;
    if (auto member = parse_record_member())
      members.push_back(std::move(*member));
    else
      synchronize_statement();
    if (cursor_ == before)
      advance();
  }
  if (!expect(TokenKind::RBrace, "expected '}' after record declaration"))
    return nullptr;
  auto *declaration = arena_->create<RecordTypeDeclaration>();
  declaration->span = span_from(start);
  declaration->is_public = is_public;
  declaration->name = std::move(name);
  declaration->generic_parameters = std::move(parameters);
  declaration->members = std::move(members);
  return declaration;
}

std::optional<RecordMember> Parser::parse_record_member() {
  if (check(TokenKind::KwInvariant)) {
    if (auto *node = parse_invariant_declaration())
      return RecordMember{node};
    return std::nullopt;
  }
  const std::size_t errors = errors_.size();
  FieldDeclaration field = parse_field_declaration();
  if (errors_.size() != errors)
    return std::nullopt;
  return RecordMember{std::move(field)};
}

FieldDeclaration Parser::parse_field_declaration() {
  FieldDeclaration field;
  field.is_public = parse_visibility();
  field.name = take(TokenKind::Identifier, "expected field name");
  expect(TokenKind::Colon, "expected ':' after field name");
  field.type = parse_type_expression();
  expect(TokenKind::SemiColon, "expected ';' after field declaration");
  return field;
}

InvariantDeclaration *Parser::parse_invariant_declaration() {
  const Token start = current();
  if (!expect(TokenKind::KwInvariant, "expected 'invariant'"))
    return nullptr;
  std::optional<Token> name;
  if (check(TokenKind::Identifier) && check(TokenKind::Colon, 1)) {
    name = current();
    advance();
    advance();
  }
  PredicatePtr predicate = parse_predicate();
  if (!predicate ||
      !expect(TokenKind::SemiColon, "expected ';' after invariant"))
    return nullptr;
  auto *declaration = arena_->create<InvariantDeclaration>();
  declaration->span = span_from(start);
  declaration->name = std::move(name);
  declaration->predicate = predicate;
  return declaration;
}

SumTypeDeclaration *Parser::parse_sum_type_declaration() {
  const Token start = current();
  const bool is_public = parse_visibility();
  if (!expect(TokenKind::KwType, "expected 'type'"))
    return nullptr;
  Token name = take(TokenKind::Identifier, "expected sum type name");
  std::vector<GenericParameter> parameters;
  if (check(TokenKind::Less))
    parameters = parse_generic_parameter_list();
  if (!expect(TokenKind::Equal, "expected '=' in sum type declaration"))
    return nullptr;
  std::vector<VariantDeclaration> variants;
  variants.push_back(parse_variant_declaration());
  if (!expect(TokenKind::Pipe, "sum type requires at least two variants"))
    return nullptr;
  variants.push_back(parse_variant_declaration());
  while (match(TokenKind::Pipe))
    variants.push_back(parse_variant_declaration());
  if (!expect(TokenKind::SemiColon, "expected ';' after sum type"))
    return nullptr;
  auto *declaration = arena_->create<SumTypeDeclaration>();
  declaration->span = span_from(start);
  declaration->is_public = is_public;
  declaration->name = std::move(name);
  declaration->generic_parameters = std::move(parameters);
  declaration->variants = std::move(variants);
  return declaration;
}

VariantDeclaration Parser::parse_variant_declaration() {
  VariantDeclaration variant{
      .name = take(TokenKind::Identifier, "expected variant name")};
  if (match(TokenKind::LParen)) {
    std::vector<VariantField> fields;
    if (!check(TokenKind::RParen))
      fields = parse_variant_field_list();
    expect(TokenKind::RParen, "expected ')' after variant fields");
    variant.fields = std::move(fields);
  }
  return variant;
}

std::vector<VariantField> Parser::parse_variant_field_list() {
  std::vector<VariantField> fields;
  fields.push_back(parse_variant_field());
  while (match(TokenKind::Comma))
    fields.push_back(parse_variant_field());
  return fields;
}

VariantField Parser::parse_variant_field() {
  VariantField field;
  if (check(TokenKind::Identifier) && check(TokenKind::Colon, 1)) {
    field.name = current();
    advance();
    advance();
  }
  field.type = parse_type_expression();
  return field;
}

TypePtr Parser::parse_type_expression() { return parse_union_type(); }

TypePtr Parser::parse_union_type() {
  TypePtr first = parse_function_type();
  if (!first)
    return nullptr;
  if (!check(TokenKind::Pipe))
    return first;
  std::vector<TypePtr> members{first};
  while (match(TokenKind::Pipe)) {
    TypePtr member = parse_function_type();
    if (!member)
      return nullptr;
    members.push_back(member);
  }
  return make_node<TypeExpression>(
      *arena_, {.start = first->span.start, .end = members.back()->span.end},
      UnionType{.members = std::move(members)});
}

TypePtr Parser::parse_function_type() {
  return check(TokenKind::KwFn) ? parse_function_type_expression()
                                : parse_type_postfix_expression();
}

TypePtr Parser::parse_function_type_expression() {
  const Token start = current();
  if (!expect(TokenKind::KwFn, "expected 'fn' in function type") ||
      !expect(TokenKind::LParen, "expected '(' in function type"))
    return nullptr;
  std::vector<TypePtr> parameters;
  if (!check(TokenKind::RParen))
    parameters = parse_type_expression_list();
  if (!expect(TokenKind::RParen, "expected ')' after function parameters") ||
      !expect(TokenKind::Arrow, "expected '->' in function type"))
    return nullptr;
  TypePtr result = parse_type_expression();
  if (!result)
    return nullptr;
  auto effects = parse_function_type_effects();
  return make_node<TypeExpression>(
      *arena_, span_from(start),
      FunctionType{.parameters = std::move(parameters),
                   .result = result,
                   .effects = std::move(effects)});
}

std::vector<EffectReference> Parser::parse_function_type_effects() {
  if (!match(TokenKind::KwUses))
    return {};
  if (!expect(TokenKind::LBrace, "expected '{' after 'uses'"))
    return {};
  std::vector<EffectReference> effects;
  if (!check(TokenKind::RBrace))
    effects = parse_effect_reference_list();
  expect(TokenKind::RBrace, "expected '}' after function type effects");
  return effects;
}

std::vector<TypePtr> Parser::parse_type_expression_list() {
  std::vector<TypePtr> types;
  if (TypePtr type = parse_type_expression())
    types.push_back(type);
  while (match(TokenKind::Comma)) {
    if (TypePtr type = parse_type_expression())
      types.push_back(type);
  }
  return types;
}

TypePtr Parser::parse_type_postfix_expression() {
  TypePtr base = parse_type_primary();
  if (!base)
    return nullptr;
  while (check(TokenKind::Less)) {
    auto arguments = parse_type_postfix();
    base = make_node<TypeExpression>(
        *arena_, {.start = base->span.start, .end = token_end(previous())},
        AppliedType{.base = base, .arguments = std::move(arguments)});
  }
  return base;
}

std::vector<MetaArgument> Parser::parse_type_postfix() {
  return parse_meta_argument_list();
}

TypePtr Parser::parse_type_primary() {
  if (check(TokenKind::Identifier)) {
    const Token start = current();
    QualifiedName name = parse_qualified_name();
    return make_node<TypeExpression>(*arena_, span_from(start),
                                     TypeName{.name = std::move(name)});
  }
  if (check(TokenKind::LBrace))
    return parse_structural_record_type();
  if (check(TokenKind::LParen)) {
    std::size_t index = cursor_ + 1;
    std::size_t depth = 0;
    bool comma = false;
    while (index < tokens_.size()) {
      if (tokens_[index].kind == TokenKind::LParen)
        ++depth;
      else if (tokens_[index].kind == TokenKind::RParen) {
        if (depth == 0)
          break;
        --depth;
      } else if (tokens_[index].kind == TokenKind::Comma && depth == 0) {
        comma = true;
        break;
      }
      ++index;
    }
    if (comma)
      return parse_tuple_type();
    const Token start = current();
    advance();
    TypePtr type = parse_type_expression();
    if (!type || !expect(TokenKind::RParen, "expected ')' after type"))
      return nullptr;
    return make_node<TypeExpression>(*arena_, span_from(start),
                                     ParenthesizedType{.type = type});
  }
  report_error(current(), "expected a type expression");
  return nullptr;
}

TypePtr Parser::parse_tuple_type() {
  const Token start = current();
  expect(TokenKind::LParen, "expected '('");
  std::vector<TypePtr> elements;
  TypePtr first = parse_type_expression();
  if (!first)
    return nullptr;
  elements.push_back(first);
  if (!expect(TokenKind::Comma, "tuple type requires at least two elements"))
    return nullptr;
  TypePtr second = parse_type_expression();
  if (!second)
    return nullptr;
  elements.push_back(second);
  while (match(TokenKind::Comma)) {
    TypePtr element = parse_type_expression();
    if (!element)
      return nullptr;
    elements.push_back(element);
  }
  if (!expect(TokenKind::RParen, "expected ')' after tuple type"))
    return nullptr;
  return make_node<TypeExpression>(*arena_, span_from(start),
                                   TupleType{.elements = std::move(elements)});
}

TypePtr Parser::parse_structural_record_type() {
  const Token start = current();
  expect(TokenKind::LBrace, "expected '{'");
  std::vector<RowField> fields;
  std::optional<Token> tail;
  if (!check(TokenKind::RBrace) && !check(TokenKind::Pipe))
    fields = parse_row_field_list();
  if (match(TokenKind::Pipe))
    tail = parse_row_tail();
  if (!expect(TokenKind::RBrace, "expected '}' after structural record type"))
    return nullptr;
  return make_node<TypeExpression>(
      *arena_, span_from(start),
      StructuralRecordType{.fields = std::move(fields),
                           .tail = std::move(tail)});
}

std::vector<RowField> Parser::parse_row_field_list() {
  std::vector<RowField> fields;
  fields.push_back(parse_row_field());
  while (match(TokenKind::Comma)) {
    if (check(TokenKind::Pipe) || check(TokenKind::RBrace))
      break;
    fields.push_back(parse_row_field());
  }
  return fields;
}

RowField Parser::parse_row_field() {
  RowField field{.name =
                     take(TokenKind::Identifier, "expected row field name")};
  expect(TokenKind::Colon, "expected ':' after row field name");
  // At row level, an unparenthesized pipe introduces the row tail.
  field.type = parse_function_type();
  return field;
}

Token Parser::parse_row_tail() {
  return take(TokenKind::Identifier, "expected row tail name");
}

TraitDeclaration *Parser::parse_trait_declaration() {
  const Token start = current();
  const bool is_public = parse_visibility();
  if (!expect(TokenKind::KwTrait, "expected 'trait'"))
    return nullptr;
  Token name = take(TokenKind::Identifier, "expected trait name");
  std::vector<GenericParameter> parameters;
  if (check(TokenKind::Less))
    parameters = parse_generic_parameter_list();
  if (!expect(TokenKind::LBrace, "expected '{' in trait declaration"))
    return nullptr;
  std::vector<TraitMember> members;
  while (!check(TokenKind::RBrace) && !is_at_end()) {
    const std::size_t before = cursor_;
    if (auto member = parse_trait_member())
      members.push_back(std::move(*member));
    else
      synchronize_statement();
    if (cursor_ == before)
      advance();
  }
  if (!expect(TokenKind::RBrace, "expected '}' after trait declaration"))
    return nullptr;
  auto *declaration = arena_->create<TraitDeclaration>();
  declaration->span = span_from(start);
  declaration->is_public = is_public;
  declaration->name = std::move(name);
  declaration->generic_parameters = std::move(parameters);
  declaration->members = std::move(members);
  return declaration;
}

std::optional<TraitMember> Parser::parse_trait_member() {
  if (check(TokenKind::KwFn))
    return TraitMember{parse_trait_function_declaration()};
  if (check(TokenKind::KwLaw)) {
    if (auto *law = parse_law_declaration())
      return TraitMember{law};
    return std::nullopt;
  }
  report_error(current(), "expected trait function or law");
  return std::nullopt;
}

TraitFunctionDeclaration Parser::parse_trait_function_declaration() {
  TraitFunctionDeclaration declaration;
  auto signature = parse_function_signature();
  const bool has_clauses = signature && !signature->clauses.empty();
  if (signature)
    declaration.signature = std::move(*signature);
  if (!has_clauses)
    expect(TokenKind::SemiColon, "expected ';' after trait function");
  else
    match(TokenKind::SemiColon);
  return declaration;
}

LawDeclaration *Parser::parse_law_declaration() {
  const Token start = current();
  if (!expect(TokenKind::KwLaw, "expected 'law'"))
    return nullptr;
  Token name = take(TokenKind::Identifier, "expected law name");
  if (!expect(TokenKind::Colon, "expected ':' after law name"))
    return nullptr;
  PredicatePtr predicate = parse_predicate();
  if (!predicate || !expect(TokenKind::SemiColon, "expected ';' after law"))
    return nullptr;
  auto *law = arena_->create<LawDeclaration>();
  law->span = span_from(start);
  law->name = std::move(name);
  law->predicate = predicate;
  return law;
}

ImplDeclaration *Parser::parse_impl_declaration() {
  const Token start = current();
  const bool is_public = parse_visibility();
  if (!expect(TokenKind::KwImpl, "expected 'impl'"))
    return nullptr;
  TraitReference trait = parse_trait_reference();
  TypePtr target = nullptr;
  if (match(TokenKind::KwFor))
    target = parse_type_expression();
  if (!expect(TokenKind::LBrace, "expected '{' in impl declaration"))
    return nullptr;
  std::vector<FunctionDeclaration *> functions;
  while (!check(TokenKind::RBrace) && !is_at_end()) {
    const std::size_t before = cursor_;
    if (auto *function = parse_impl_member())
      functions.push_back(function);
    else
      synchronize_declaration();
    if (cursor_ == before)
      advance();
  }
  if (!expect(TokenKind::RBrace, "expected '}' after impl declaration"))
    return nullptr;
  auto *declaration = arena_->create<ImplDeclaration>();
  declaration->span = span_from(start);
  declaration->is_public = is_public;
  declaration->trait = std::move(trait);
  declaration->target = target;
  declaration->functions = std::move(functions);
  return declaration;
}

TraitReference Parser::parse_trait_reference() {
  TraitReference reference{.name = parse_qualified_name()};
  if (check(TokenKind::Less))
    reference.arguments = parse_meta_argument_list();
  return reference;
}

FunctionDeclaration *Parser::parse_impl_member() {
  return parse_function_declaration();
}

EffectDeclaration *Parser::parse_effect_declaration() {
  const Token start = current();
  const bool is_public = parse_visibility();
  if (!expect(TokenKind::KwEffect, "expected 'effect'"))
    return nullptr;
  Token name = take(TokenKind::Identifier, "expected effect name");
  std::optional<std::vector<CapabilityDeclaration>> capabilities;
  if (match(TokenKind::LBrace)) {
    capabilities.emplace();
    while (!check(TokenKind::RBrace) && !is_at_end()) {
      const std::size_t before = cursor_;
      if (check(TokenKind::KwCapability))
        capabilities->push_back(parse_capability_declaration());
      else {
        report_error(current(), "expected a capability declaration");
        synchronize_statement();
      }
      if (cursor_ == before)
        advance();
    }
    if (!expect(TokenKind::RBrace, "expected '}' after effect declaration"))
      return nullptr;
  }
  auto *declaration = arena_->create<EffectDeclaration>();
  declaration->span = span_from(start);
  declaration->is_public = is_public;
  declaration->name = std::move(name);
  declaration->capabilities = std::move(capabilities);
  return declaration;
}

CapabilityDeclaration Parser::parse_capability_declaration() {
  expect(TokenKind::KwCapability, "expected 'capability'");
  CapabilityDeclaration declaration{
      .name = take(TokenKind::Identifier, "expected capability name")};
  expect(TokenKind::SemiColon, "expected ';' after capability");
  return declaration;
}

DomainDeclaration *Parser::parse_domain_declaration() {
  const Token start = current();
  const bool is_public = parse_visibility();
  if (!expect(TokenKind::KwDomain, "expected 'domain'"))
    return nullptr;
  Token name = take(TokenKind::Identifier, "expected domain name");
  if (!expect(TokenKind::LBrace, "expected '{' in domain declaration"))
    return nullptr;
  std::vector<DomainMember> members;
  while (!check(TokenKind::RBrace) && !is_at_end()) {
    const std::size_t before = cursor_;
    if (auto member = parse_domain_member())
      members.push_back(std::move(*member));
    else
      synchronize_declaration();
    if (cursor_ == before)
      advance();
  }
  if (!expect(TokenKind::RBrace, "expected '}' after domain declaration"))
    return nullptr;
  auto *declaration = arena_->create<DomainDeclaration>();
  declaration->span = span_from(start);
  declaration->is_public = is_public;
  declaration->name = std::move(name);
  declaration->members = std::move(members);
  return declaration;
}

std::optional<DomainMember> Parser::parse_domain_member() {
  if (check(TokenKind::KwInvariant)) {
    if (auto *node = parse_invariant_declaration())
      return DomainMember{node};
  } else {
    const std::size_t offset = check(TokenKind::KwPub) ? 1 : 0;
    if (check(TokenKind::KwType, offset)) {
      if (auto type = parse_type_declaration())
        return DomainMember{std::move(*type)};
    } else if (check(TokenKind::KwFn, offset) ||
               check(TokenKind::KwTotal, offset)) {
      if (auto *node = parse_function_declaration())
        return DomainMember{node};
    } else {
      report_error(current(), "expected a domain member");
    }
  }
  return std::nullopt;
}

Token Parser::parse_boolean_literal() {
  if (check(TokenKind::KwTrue) || check(TokenKind::KwFalse)) {
    Token token = current();
    advance();
    return token;
  }
  return take(TokenKind::KwTrue, "expected boolean literal");
}
Token Parser::parse_integer_literal() {
  return take(TokenKind::IntegerLiteral, "expected integer literal");
}
Token Parser::parse_floating_literal() {
  return take(TokenKind::FloatLiteral, "expected floating literal");
}
Token Parser::parse_character_literal() {
  return take(TokenKind::CharacterLiteral, "expected character literal");
}
Token Parser::parse_string_literal() {
  return take(TokenKind::StringLiteral, "expected string literal");
}
Token Parser::parse_identifier() {
  return take(TokenKind::Identifier, "expected identifier");
}

} // namespace flux::parser

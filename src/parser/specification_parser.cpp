#include "flux/parser/parser.hpp"

#include <utility>

namespace flux::parser {
namespace {

SourceLocation end_of(const Token &token) {
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

template <typename NodeType, typename Value>
NodeType *make_node(BumpAllocator &arena, SourceSpan span, Value value) {
  auto *node = arena.create<NodeType>();
  node->span = span;
  node->value = std::move(value);
  return node;
}

bool literal_kind(TokenKind kind) {
  switch (kind) {
  case TokenKind::KwTrue:
  case TokenKind::KwFalse:
  case TokenKind::IntegerLiteral:
  case TokenKind::FloatLiteral:
  case TokenKind::CharacterLiteral:
  case TokenKind::StringLiteral:
    return true;
  default:
    return false;
  }
}

bool comparison_kind(TokenKind kind) {
  switch (kind) {
  case TokenKind::DoubleEqual:
  case TokenKind::BangEqual:
  case TokenKind::Less:
  case TokenKind::LessEqual:
  case TokenKind::Greater:
  case TokenKind::GreaterEqual:
    return true;
  default:
    return false;
  }
}

} // namespace

PredicatePtr Parser::parse_predicate() { return parse_predicate_implication(); }

PredicatePtr Parser::parse_predicate_implication() {
  PredicatePtr left = parse_predicate_equivalence();
  if (!left || !match(TokenKind::BigArrow))
    return left;
  Token op = previous();
  PredicatePtr right = parse_predicate_implication();
  if (!right)
    return nullptr;
  return make_node<Predicate>(
      *arena_, {.start = left->span.start, .end = right->span.end},
      BinaryPredicate{.left = left, .op = std::move(op), .right = right});
}

PredicatePtr Parser::parse_predicate_equivalence() {
  PredicatePtr left = parse_predicate_or();
  while (left && match(TokenKind::Equivalence)) {
    Token op = previous();
    PredicatePtr right = parse_predicate_or();
    if (!right)
      return nullptr;
    left = make_node<Predicate>(
        *arena_, {.start = left->span.start, .end = right->span.end},
        BinaryPredicate{.left = left, .op = std::move(op), .right = right});
  }
  return left;
}

PredicatePtr Parser::parse_predicate_or() {
  PredicatePtr left = parse_predicate_and();
  while (left && match(TokenKind::LogicalOr)) {
    Token op = previous();
    PredicatePtr right = parse_predicate_and();
    if (!right)
      return nullptr;
    left = make_node<Predicate>(
        *arena_, {.start = left->span.start, .end = right->span.end},
        BinaryPredicate{.left = left, .op = std::move(op), .right = right});
  }
  return left;
}

PredicatePtr Parser::parse_predicate_and() {
  PredicatePtr left = parse_predicate_unary();
  while (left && match(TokenKind::LogicalAnd)) {
    Token op = previous();
    PredicatePtr right = parse_predicate_unary();
    if (!right)
      return nullptr;
    left = make_node<Predicate>(
        *arena_, {.start = left->span.start, .end = right->span.end},
        BinaryPredicate{.left = left, .op = std::move(op), .right = right});
  }
  return left;
}

PredicatePtr Parser::parse_predicate_unary() {
  if (match(TokenKind::Bang)) {
    const Token op = previous();
    PredicatePtr predicate = parse_predicate_unary();
    if (!predicate)
      return nullptr;
    return make_node<Predicate>(
        *arena_, {.start = op.location, .end = predicate->span.end},
        NegatedPredicate{.predicate = predicate});
  }
  if (check(TokenKind::KwForall) || check(TokenKind::KwExists))
    return parse_quantified_predicate();
  return parse_predicate_atom();
}

PredicatePtr Parser::parse_quantified_predicate() {
  const Token start = current();
  Quantifier quantifier = parse_quantifier();
  auto bindings = parse_quantifier_binding_list();
  if (!expect(TokenKind::LBrace, "expected '{' after quantifier bindings"))
    return nullptr;
  PredicatePtr predicate = parse_predicate();
  if (!predicate ||
      !expect(TokenKind::RBrace, "expected '}' after quantified predicate"))
    return nullptr;
  return make_node<Predicate>(
      *arena_, span_from(start),
      QuantifiedPredicate{.quantifier = quantifier,
                          .bindings = std::move(bindings),
                          .predicate = predicate});
}

Quantifier Parser::parse_quantifier() {
  if (match(TokenKind::KwExists))
    return Quantifier::Exists;
  expect(TokenKind::KwForall, "expected 'forall' or 'exists'");
  return Quantifier::Forall;
}

std::vector<QuantifierBinding> Parser::parse_quantifier_binding_list() {
  std::vector<QuantifierBinding> bindings;
  bindings.push_back(parse_quantifier_binding());
  while (match(TokenKind::Comma))
    bindings.push_back(parse_quantifier_binding());
  return bindings;
}

QuantifierBinding Parser::parse_quantifier_binding() {
  QuantifierBinding binding{
      .name = take(TokenKind::Identifier, "expected quantified name")};
  expect(TokenKind::Colon, "expected ':' after quantified name");
  binding.domain = parse_quantifier_domain();
  return binding;
}

std::variant<TypePtr, KindPtr> Parser::parse_quantifier_domain() {
  if (looks_like_kind_expression())
    return parse_kind_expression();
  return parse_type_expression();
}

PredicatePtr Parser::parse_predicate_atom() {
  const std::size_t saved_cursor = cursor_;
  const std::size_t saved_errors = errors_.size();
  SpecExprPtr left = parse_spec_expression();
  if (left && comparison_kind(current().kind)) {
    Token op = parse_predicate_comparison_operator();
    SpecExprPtr right = parse_spec_expression();
    if (!right)
      return nullptr;
    return make_node<Predicate>(
        *arena_, {.start = left->span.start, .end = right->span.end},
        ComparisonPredicate{.left = left, .op = std::move(op), .right = right});
  }
  cursor_ = saved_cursor;
  errors_.resize(saved_errors);

  if (check(TokenKind::KwTrue) || check(TokenKind::KwFalse)) {
    Token literal = current();
    advance();
    return make_node<Predicate>(
        *arena_, {.start = literal.location, .end = end_of(literal)},
        BooleanPredicate{.literal = std::move(literal)});
  }
  if (match(TokenKind::LParen)) {
    const Token start = previous();
    PredicatePtr predicate = parse_predicate();
    if (!predicate ||
        !expect(TokenKind::RParen, "expected ')' after predicate"))
      return nullptr;
    return make_node<Predicate>(*arena_, span_from(start),
                                ParenthesizedPredicate{.predicate = predicate});
  }
  if (check(TokenKind::KwOld)) {
    const Token start = current();
    SpecExprPtr expression = parse_old_expression();
    if (!expression)
      return nullptr;
    return make_node<Predicate>(
        *arena_, {.start = start.location, .end = expression->span.end},
        OldPredicate{.expression = expression});
  }

  if (check(TokenKind::Identifier))
    return parse_predicate_call();
  report_error(current(), "expected a predicate");
  return nullptr;
}

PredicatePtr Parser::parse_predicate_comparison() {
  SpecExprPtr left = parse_spec_expression();
  if (!left || !comparison_kind(current().kind)) {
    report_error(current(), "expected predicate comparison operator");
    return nullptr;
  }
  Token op = parse_predicate_comparison_operator();
  SpecExprPtr right = parse_spec_expression();
  if (!right)
    return nullptr;
  return make_node<Predicate>(
      *arena_, {.start = left->span.start, .end = right->span.end},
      ComparisonPredicate{.left = left, .op = std::move(op), .right = right});
}

Token Parser::parse_predicate_comparison_operator() {
  if (comparison_kind(current().kind)) {
    Token op = current();
    advance();
    return op;
  }
  return take(TokenKind::DoubleEqual, "expected comparison operator");
}

PredicatePtr Parser::parse_predicate_call() {
  const Token start = current();
  QualifiedName callee = parse_qualified_name();
  if (!expect(TokenKind::LParen, "expected '(' in predicate call"))
    return nullptr;
  std::vector<SpecExprPtr> arguments;
  if (!check(TokenKind::RParen))
    arguments = parse_spec_expression_list();
  if (!expect(TokenKind::RParen, "expected ')' after predicate arguments"))
    return nullptr;
  return make_node<Predicate>(*arena_, span_from(start),
                              CallPredicate{.callee = std::move(callee),
                                            .arguments = std::move(arguments)});
}

SpecExprPtr Parser::parse_spec_expression() { return parse_spec_additive(); }

SpecExprPtr Parser::parse_spec_additive() {
  SpecExprPtr left = parse_spec_multiplicative();
  while (left && (check(TokenKind::Plus) || check(TokenKind::Minus))) {
    Token op = current();
    advance();
    SpecExprPtr right = parse_spec_multiplicative();
    if (!right)
      return nullptr;
    left = make_node<SpecificationExpression>(
        *arena_, {.start = left->span.start, .end = right->span.end},
        BinarySpecExpression{
            .left = left, .op = std::move(op), .right = right});
  }
  return left;
}

SpecExprPtr Parser::parse_spec_multiplicative() {
  SpecExprPtr left = parse_spec_unary();
  while (left && (check(TokenKind::Star) || check(TokenKind::Slash) ||
                  check(TokenKind::Percent))) {
    Token op = current();
    advance();
    SpecExprPtr right = parse_spec_unary();
    if (!right)
      return nullptr;
    left = make_node<SpecificationExpression>(
        *arena_, {.start = left->span.start, .end = right->span.end},
        BinarySpecExpression{
            .left = left, .op = std::move(op), .right = right});
  }
  return left;
}

SpecExprPtr Parser::parse_spec_unary() {
  if (check(TokenKind::Plus) || check(TokenKind::Minus)) {
    Token op = current();
    advance();
    SpecExprPtr operand = parse_spec_unary();
    if (!operand)
      return nullptr;
    return make_node<SpecificationExpression>(
        *arena_, {.start = op.location, .end = operand->span.end},
        UnarySpecExpression{.op = std::move(op), .operand = operand});
  }
  return parse_spec_postfix();
}

SpecExprPtr Parser::parse_spec_postfix() {
  SpecExprPtr base = parse_spec_primary();
  if (!base)
    return nullptr;
  std::vector<SpecPostfixOperation> operations;
  while (true) {
    if (match(TokenKind::Dot)) {
      operations.emplace_back(SpecFieldOperation{
          .field = take(TokenKind::Identifier, "expected field name")});
    } else if (match(TokenKind::LBracket)) {
      SpecExprPtr index = parse_spec_expression();
      expect(TokenKind::RBracket, "expected ']' after specification index");
      operations.emplace_back(SpecIndexOperation{.index = index});
    } else if (match(TokenKind::LParen)) {
      std::vector<SpecExprPtr> arguments;
      if (!check(TokenKind::RParen))
        arguments = parse_spec_expression_list();
      expect(TokenKind::RParen, "expected ')' after specification arguments");
      operations.emplace_back(
          SpecCallOperation{.arguments = std::move(arguments)});
    } else {
      break;
    }
  }
  if (operations.empty())
    return base;
  return make_node<SpecificationExpression>(
      *arena_, {.start = base->span.start, .end = end_of(previous())},
      PostfixSpecExpression{.base = base, .operations = std::move(operations)});
}

SpecExprPtr Parser::parse_spec_primary() {
  const Token start = current();
  if (literal_kind(current().kind)) {
    Token literal = current();
    advance();
    return make_node<SpecificationExpression>(
        *arena_, span_from(start),
        LiteralSpecExpression{.literal = std::move(literal)});
  }
  if (check(TokenKind::Identifier)) {
    Token name = current();
    advance();
    return make_node<SpecificationExpression>(
        *arena_, span_from(start), NameSpecExpression{.name = std::move(name)});
  }
  if (match(TokenKind::KwSelf)) {
    return make_node<SpecificationExpression>(
        *arena_, span_from(start),
        NameSpecExpression{.name = SpecialSpecificationName::Self});
  }
  if (match(TokenKind::KwResult)) {
    return make_node<SpecificationExpression>(
        *arena_, span_from(start),
        NameSpecExpression{.name = SpecialSpecificationName::Result});
  }
  if (check(TokenKind::KwOld))
    return parse_old_expression();
  if (match(TokenKind::LParen)) {
    SpecExprPtr expression = parse_spec_expression();
    if (!expression || !expect(TokenKind::RParen,
                               "expected ')' after specification expression"))
      return nullptr;
    return make_node<SpecificationExpression>(
        *arena_, span_from(start),
        ParenthesizedSpecExpression{.expression = expression});
  }
  report_error(current(), "expected a specification expression");
  return nullptr;
}

std::vector<SpecExprPtr> Parser::parse_spec_expression_list() {
  std::vector<SpecExprPtr> expressions;
  if (SpecExprPtr first = parse_spec_expression())
    expressions.push_back(first);
  while (match(TokenKind::Comma)) {
    if (SpecExprPtr next = parse_spec_expression())
      expressions.push_back(next);
  }
  return expressions;
}

SpecExprPtr Parser::parse_old_expression() {
  const Token start = current();
  if (!expect(TokenKind::KwOld, "expected 'old'") ||
      !expect(TokenKind::LParen, "expected '(' after 'old'"))
    return nullptr;
  SpecExprPtr expression = parse_spec_expression();
  if (!expression ||
      !expect(TokenKind::RParen, "expected ')' after old expression"))
    return nullptr;
  return make_node<SpecificationExpression>(
      *arena_, span_from(start), OldSpecExpression{.expression = expression});
}

} // namespace flux::parser

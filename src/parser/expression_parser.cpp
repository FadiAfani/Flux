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

} // namespace

ExprPtr Parser::parse_nested_expression() {
  const bool saved = stop_before_block_;
  stop_before_block_ = false;
  ExprPtr expression = parse_expr();
  stop_before_block_ = saved;
  return expression;
}

BlockPtr Parser::parse_block() {
  if (!check(TokenKind::LBrace)) {
    report_error(current(), "expected a block");
    return nullptr;
  }
  const Token start = current();
  advance();
  auto *block = arena_->create<Block>();

  while (!check(TokenKind::RBrace) && !is_at_end()) {
    const std::size_t before = cursor_;
    if (check(TokenKind::KwLet) || check(TokenKind::KwVar) ||
        check(TokenKind::KwReturn) || check(TokenKind::KwBreak) ||
        check(TokenKind::KwContinue) || check(TokenKind::KwWhile) ||
        check(TokenKind::KwFor) || check(TokenKind::KwMutate) ||
        check(TokenKind::KwTransaction) || check(TokenKind::KwParallel) ||
        check(TokenKind::KwUnsafe) || looks_like_assignment()) {
      if (StmtPtr statement = parse_statement()) {
        block->statements.push_back(statement);
      } else {
        synchronize_statement();
      }
    } else {
      ExprPtr expression = parse_expr();
      if (!expression) {
        synchronize_statement();
      } else if (match(TokenKind::SemiColon)) {
        block->statements.push_back(make_node<Statement>(
            *arena_,
            {.start = expression->span.start, .end = end_of(previous())},
            ExpressionStatement{.expression = expression}));
      } else {
        block->tail_expression = expression;
        break;
      }
    }
    if (cursor_ == before)
      advance();
  }

  if (!expect(TokenKind::RBrace, "expected '}' after block"))
    return nullptr;
  block->span = {.start = start.location, .end = end_of(previous())};
  return block;
}

StmtPtr Parser::parse_statement() {
  switch (current().kind) {
  case TokenKind::KwLet:
    return parse_let_statement();
  case TokenKind::KwVar:
    return parse_var_statement();
  case TokenKind::KwReturn:
    return parse_return_statement();
  case TokenKind::KwBreak:
    return parse_break_statement();
  case TokenKind::KwContinue:
    return parse_continue_statement();
  case TokenKind::KwWhile:
    return parse_while_statement();
  case TokenKind::KwFor:
    return parse_for_statement();
  case TokenKind::KwMutate:
    return parse_mutate_statement();
  case TokenKind::KwTransaction:
    return parse_transaction_statement();
  case TokenKind::KwParallel:
    return parse_parallel_statement();
  case TokenKind::KwUnsafe:
    return parse_unsafe_statement();
  default:
    return looks_like_assignment() ? parse_assignment_statement()
                                   : parse_expression_statement();
  }
}

StmtPtr Parser::parse_let_statement() {
  const Token start = current();
  expect(TokenKind::KwLet, "expected 'let'");
  PatternPtr pattern = parse_pattern();
  if (!pattern)
    return nullptr;
  TypePtr type = nullptr;
  if (match(TokenKind::Colon)) {
    type = parse_type_expression();
    if (!type)
      return nullptr;
  }
  if (!expect(TokenKind::Equal, "expected '=' in let statement"))
    return nullptr;
  ExprPtr value = parse_expr();
  if (!value ||
      !expect(TokenKind::SemiColon, "expected ';' after let statement"))
    return nullptr;
  return make_node<Statement>(
      *arena_, span_from(start),
      LetStatement{.pattern = pattern, .type = type, .value = value});
}

StmtPtr Parser::parse_var_statement() {
  const Token start = current();
  expect(TokenKind::KwVar, "expected 'var'");
  Token name = take(TokenKind::Identifier, "expected variable name");
  TypePtr type = nullptr;
  if (match(TokenKind::Colon)) {
    type = parse_type_expression();
    if (!type)
      return nullptr;
  }
  if (!expect(TokenKind::Equal, "expected '=' in var statement"))
    return nullptr;
  ExprPtr value = parse_expr();
  if (!value ||
      !expect(TokenKind::SemiColon, "expected ';' after var statement"))
    return nullptr;
  return make_node<Statement>(
      *arena_, span_from(start),
      VarStatement{.name = std::move(name), .type = type, .value = value});
}

StmtPtr Parser::parse_assignment_statement() {
  const Token start = current();
  LValue target = parse_lvalue();
  Token op = parse_assignment_operator();
  ExprPtr value = parse_expr();
  if (!value || !expect(TokenKind::SemiColon, "expected ';' after assignment"))
    return nullptr;
  return make_node<Statement>(*arena_, span_from(start),
                              AssignmentStatement{.target = std::move(target),
                                                  .op = std::move(op),
                                                  .value = value});
}

Token Parser::parse_assignment_operator() {
  switch (current().kind) {
  case TokenKind::Equal:
  case TokenKind::PlusEqual:
  case TokenKind::MinusEqual:
  case TokenKind::StarEqual:
  case TokenKind::SlashEqual:
  case TokenKind::PercentEqual: {
    Token op = current();
    advance();
    return op;
  }
  default:
    return take(TokenKind::Equal, "expected assignment operator");
  }
}

LValue Parser::parse_lvalue() {
  LValue value{.name =
                   take(TokenKind::Identifier, "expected assignment target")};
  while (check(TokenKind::Dot) || check(TokenKind::LBracket))
    value.postfixes.push_back(parse_lvalue_postfix());
  return value;
}

LValuePostfix Parser::parse_lvalue_postfix() {
  if (match(TokenKind::Dot))
    return LValueField{.field =
                           take(TokenKind::Identifier, "expected field name")};
  expect(TokenKind::LBracket, "expected '['");
  ExprPtr index = parse_nested_expression();
  expect(TokenKind::RBracket, "expected ']' after index");
  return LValueIndex{.index = index};
}

StmtPtr Parser::parse_return_statement() {
  const Token start = current();
  advance();
  ExprPtr value = check(TokenKind::SemiColon) ? nullptr : parse_expr();
  if (!expect(TokenKind::SemiColon, "expected ';' after return"))
    return nullptr;
  return make_node<Statement>(*arena_, span_from(start),
                              ReturnStatement{.value = value});
}

StmtPtr Parser::parse_break_statement() {
  const Token start = current();
  advance();
  ExprPtr value = check(TokenKind::SemiColon) ? nullptr : parse_expr();
  if (!expect(TokenKind::SemiColon, "expected ';' after break"))
    return nullptr;
  return make_node<Statement>(*arena_, span_from(start),
                              BreakStatement{.value = value});
}

StmtPtr Parser::parse_continue_statement() {
  const Token start = current();
  advance();
  if (!expect(TokenKind::SemiColon, "expected ';' after continue"))
    return nullptr;
  return make_node<Statement>(*arena_, span_from(start), ContinueStatement{});
}

StmtPtr Parser::parse_while_statement() {
  const Token start = current();
  advance();
  stop_before_block_ = true;
  ExprPtr condition = parse_expr();
  stop_before_block_ = false;
  BlockPtr body = parse_block();
  if (!condition || !body)
    return nullptr;
  return make_node<Statement>(
      *arena_, {.start = start.location, .end = body->span.end},
      WhileStatement{.condition = condition, .body = body});
}

StmtPtr Parser::parse_for_statement() {
  const Token start = current();
  advance();
  PatternPtr pattern = parse_pattern();
  if (!pattern || !expect(TokenKind::KwIn, "expected 'in' in for statement"))
    return nullptr;
  stop_before_block_ = true;
  ExprPtr range = parse_expr();
  stop_before_block_ = false;
  BlockPtr body = parse_block();
  if (!range || !body)
    return nullptr;
  return make_node<Statement>(
      *arena_, {.start = start.location, .end = body->span.end},
      ForStatement{.pattern = pattern, .range = range, .body = body});
}

StmtPtr Parser::parse_mutate_statement() {
  const Token start = current();
  advance();
  stop_before_block_ = true;
  ExprPtr target = parse_expr();
  stop_before_block_ = false;
  BlockPtr body = parse_block();
  if (!target || !body)
    return nullptr;
  return make_node<Statement>(*arena_,
                              {.start = start.location, .end = body->span.end},
                              MutateStatement{.target = target, .body = body});
}

StmtPtr Parser::parse_transaction_statement() {
  const Token start = current();
  advance();
  stop_before_block_ = true;
  ExprPtr target = parse_expr();
  stop_before_block_ = false;
  BlockPtr body = parse_block();
  if (!target || !body)
    return nullptr;
  return make_node<Statement>(
      *arena_, {.start = start.location, .end = body->span.end},
      TransactionStatement{.target = target, .body = body});
}

StmtPtr Parser::parse_parallel_statement() {
  const Token start = current();
  advance();
  BlockPtr body = parse_block();
  if (!body)
    return nullptr;
  return make_node<Statement>(*arena_,
                              {.start = start.location, .end = body->span.end},
                              ParallelStatement{.body = body});
}

StmtPtr Parser::parse_unsafe_statement() {
  const Token start = current();
  advance();
  BlockPtr body = parse_block();
  if (!body)
    return nullptr;
  return make_node<Statement>(*arena_,
                              {.start = start.location, .end = body->span.end},
                              UnsafeStatement{.body = body});
}

StmtPtr Parser::parse_expression_statement() {
  ExprPtr expression = parse_expr();
  if (!expression ||
      !expect(TokenKind::SemiColon, "expected ';' after expression statement"))
    return nullptr;
  return make_node<Statement>(
      *arena_, {.start = expression->span.start, .end = end_of(previous())},
      ExpressionStatement{.expression = expression});
}

ExprPtr Parser::parse_tail_expression() { return parse_expr(); }

Expr *Parser::parse_literal() {
  if (!literal_kind(current().kind)) {
    report_error(current(), "expected a literal");
    return nullptr;
  }
  Token token = current();
  advance();
  return make_node<Expression>(*arena_,
                               {.start = token.location, .end = end_of(token)},
                               LiteralExpression{.value = std::move(token)});
}

Expr *Parser::parse_unary_expr() {
  if (!check(TokenKind::Bang) && !check(TokenKind::Plus) &&
      !check(TokenKind::Minus))
    return parse_postfix_expression();
  Token op = current();
  advance();
  ExprPtr operand = parse_unary_expr();
  if (!operand)
    return nullptr;
  return make_node<Expression>(
      *arena_, {.start = op.location, .end = operand->span.end},
      UnaryExpression{.op = std::move(op), .operand = operand});
}

Expr *Parser::parse_expr() {
  if (check(TokenKind::KwIf))
    return parse_if_expression();
  if (check(TokenKind::KwMatch))
    return parse_match_expression();
  return parse_logical_or_expression();
}

ExprPtr Parser::parse_if_expression() {
  const Token start = current();
  advance();
  stop_before_block_ = true;
  ExprPtr condition = parse_expr();
  stop_before_block_ = false;
  BlockPtr then_block = parse_block();
  if (!condition || !then_block)
    return nullptr;
  std::variant<std::monostate, BlockPtr, ExprPtr> else_branch;
  SourceLocation end = then_block->span.end;
  if (match(TokenKind::KwElse)) {
    if (check(TokenKind::KwIf)) {
      ExprPtr branch = parse_if_expression();
      if (!branch)
        return nullptr;
      end = branch->span.end;
      else_branch = branch;
    } else {
      BlockPtr branch = parse_block();
      if (!branch)
        return nullptr;
      end = branch->span.end;
      else_branch = branch;
    }
  }
  return make_node<Expression>(
      *arena_, {.start = start.location, .end = end},
      IfExpression{.condition = condition,
                   .then_block = then_block,
                   .else_branch = std::move(else_branch)});
}

ExprPtr Parser::parse_match_expression() {
  const Token start = current();
  advance();
  stop_before_block_ = true;
  ExprPtr value = parse_expr();
  stop_before_block_ = false;
  if (!value || !expect(TokenKind::LBrace, "expected '{' after match value"))
    return nullptr;
  std::vector<MatchArm> arms;
  if (check(TokenKind::RBrace)) {
    report_error(current(), "match expression requires at least one arm");
    return nullptr;
  }
  while (!check(TokenKind::RBrace) && !is_at_end()) {
    const std::size_t before = cursor_;
    arms.push_back(parse_match_arm());
    if (cursor_ == before)
      advance();
  }
  if (!expect(TokenKind::RBrace, "expected '}' after match arms"))
    return nullptr;
  return make_node<Expression>(
      *arena_, span_from(start),
      MatchExpression{.value = value, .arms = std::move(arms)});
}

MatchArm Parser::parse_match_arm() {
  MatchArm arm;
  arm.pattern = parse_pattern();
  if (match(TokenKind::KwIf))
    arm.guard = parse_expr();
  expect(TokenKind::BigArrow, "expected '=>' in match arm");
  if (check(TokenKind::LBrace))
    arm.body = parse_block();
  else
    arm.body = parse_expr();
  match(TokenKind::Comma);
  return arm;
}

ExprPtr Parser::parse_logical_or_expression() {
  ExprPtr left = parse_logical_and_expression();
  while (left && check(TokenKind::LogicalOr)) {
    Token op = current();
    advance();
    ExprPtr right = parse_logical_and_expression();
    if (!right)
      return nullptr;
    left = make_node<Expression>(
        *arena_, {.start = left->span.start, .end = right->span.end},
        BinaryExpression{.left = left, .op = op, .right = right});
  }
  return left;
}

ExprPtr Parser::parse_logical_and_expression() {
  ExprPtr left = parse_equality_expression();
  while (left && check(TokenKind::LogicalAnd)) {
    Token op = current();
    advance();
    ExprPtr right = parse_equality_expression();
    if (!right)
      return nullptr;
    left = make_node<Expression>(
        *arena_, {.start = left->span.start, .end = right->span.end},
        BinaryExpression{.left = left, .op = op, .right = right});
  }
  return left;
}

ExprPtr Parser::parse_equality_expression() {
  ExprPtr left = parse_comparison_expression();
  while (left &&
         (check(TokenKind::DoubleEqual) || check(TokenKind::BangEqual))) {
    Token op = parse_equality_operator();
    ExprPtr right = parse_comparison_expression();
    if (!right)
      return nullptr;
    left = make_node<Expression>(
        *arena_, {.start = left->span.start, .end = right->span.end},
        BinaryExpression{.left = left, .op = op, .right = right});
  }
  return left;
}

Token Parser::parse_equality_operator() {
  if (check(TokenKind::DoubleEqual) || check(TokenKind::BangEqual)) {
    Token op = current();
    advance();
    return op;
  }
  return take(TokenKind::DoubleEqual, "expected equality operator");
}

ExprPtr Parser::parse_comparison_expression() {
  ExprPtr left = parse_range_expression();
  if (!left)
    return nullptr;
  if (check(TokenKind::Less) || check(TokenKind::LessEqual) ||
      check(TokenKind::Greater) || check(TokenKind::GreaterEqual)) {
    Token op = parse_comparison_operator();
    ExprPtr right = parse_range_expression();
    if (!right)
      return nullptr;
    left = make_node<Expression>(
        *arena_, {.start = left->span.start, .end = right->span.end},
        BinaryExpression{.left = left, .op = op, .right = right});
    if (check(TokenKind::Less) || check(TokenKind::LessEqual) ||
        check(TokenKind::Greater) || check(TokenKind::GreaterEqual))
      report_error(current(), "chained comparisons are not allowed");
  }
  return left;
}

Token Parser::parse_comparison_operator() {
  switch (current().kind) {
  case TokenKind::Less:
  case TokenKind::LessEqual:
  case TokenKind::Greater:
  case TokenKind::GreaterEqual: {
    Token op = current();
    advance();
    return op;
  }
  default:
    return take(TokenKind::Less, "expected comparison operator");
  }
}

ExprPtr Parser::parse_range_expression() {
  ExprPtr left = parse_additive_expression();
  if (left && (check(TokenKind::DotDot) || check(TokenKind::DotDotEqual))) {
    Token op = parse_range_operator();
    ExprPtr right = parse_additive_expression();
    if (!right)
      return nullptr;
    left = make_node<Expression>(
        *arena_, {.start = left->span.start, .end = right->span.end},
        BinaryExpression{.left = left, .op = op, .right = right});
  }
  return left;
}

Token Parser::parse_range_operator() {
  if (check(TokenKind::DotDot) || check(TokenKind::DotDotEqual)) {
    Token op = current();
    advance();
    return op;
  }
  return take(TokenKind::DotDot, "expected range operator");
}

ExprPtr Parser::parse_additive_expression() {
  ExprPtr left = parse_multiplicative_expression();
  while (left && (check(TokenKind::Plus) || check(TokenKind::Minus))) {
    Token op = parse_additive_operator();
    ExprPtr right = parse_multiplicative_expression();
    if (!right)
      return nullptr;
    left = make_node<Expression>(
        *arena_, {.start = left->span.start, .end = right->span.end},
        BinaryExpression{.left = left, .op = op, .right = right});
  }
  return left;
}

Token Parser::parse_additive_operator() {
  if (check(TokenKind::Plus) || check(TokenKind::Minus)) {
    Token op = current();
    advance();
    return op;
  }
  return take(TokenKind::Plus, "expected additive operator");
}

ExprPtr Parser::parse_multiplicative_expression() {
  ExprPtr left = parse_unary_expr();
  while (left && (check(TokenKind::Star) || check(TokenKind::Slash) ||
                  check(TokenKind::Percent))) {
    Token op = parse_multiplicative_operator();
    ExprPtr right = parse_unary_expr();
    if (!right)
      return nullptr;
    left = make_node<Expression>(
        *arena_, {.start = left->span.start, .end = right->span.end},
        BinaryExpression{.left = left, .op = op, .right = right});
  }
  return left;
}

Token Parser::parse_multiplicative_operator() {
  if (check(TokenKind::Star) || check(TokenKind::Slash) ||
      check(TokenKind::Percent)) {
    Token op = current();
    advance();
    return op;
  }
  return take(TokenKind::Star, "expected multiplicative operator");
}

Token Parser::parse_unary_operator() {
  if (check(TokenKind::Bang) || check(TokenKind::Plus) ||
      check(TokenKind::Minus)) {
    Token op = current();
    advance();
    return op;
  }
  return take(TokenKind::Bang, "expected unary operator");
}

ExprPtr Parser::parse_postfix_expression() {
  ExprPtr base = parse_postfix_expression_base();
  if (!base)
    return nullptr;
  std::vector<PostfixOperation> operations;
  while (true) {
    if (check(TokenKind::LParen) || check(TokenKind::Dot) ||
        check(TokenKind::LBracket) || check(TokenKind::Question)) {
      operations.push_back(parse_postfix_operation());
      continue;
    }
    if (check(TokenKind::Less)) {
      const std::size_t saved_cursor = cursor_;
      const std::size_t saved_errors = errors_.size();
      GenericApplication generic = parse_generic_application();
      const TokenKind following = current().kind;
      const bool valid_follow =
          following == TokenKind::LParen || following == TokenKind::Dot ||
          following == TokenKind::LBracket ||
          following == TokenKind::Question || following == TokenKind::LBrace ||
          following == TokenKind::KwWith || following == TokenKind::Plus ||
          following == TokenKind::Minus || following == TokenKind::Star ||
          following == TokenKind::Slash || following == TokenKind::Percent ||
          following == TokenKind::DoubleEqual ||
          following == TokenKind::BangEqual || following == TokenKind::Less ||
          following == TokenKind::LessEqual ||
          following == TokenKind::Greater ||
          following == TokenKind::GreaterEqual ||
          following == TokenKind::LogicalAnd ||
          following == TokenKind::LogicalOr || following == TokenKind::DotDot ||
          following == TokenKind::DotDotEqual ||
          following == TokenKind::Comma || following == TokenKind::SemiColon ||
          following == TokenKind::RParen || following == TokenKind::RBracket ||
          following == TokenKind::RBrace || following == TokenKind::EndOfFile;
      if (errors_.size() == saved_errors && valid_follow) {
        operations.emplace_back(std::move(generic));
        continue;
      }
      cursor_ = saved_cursor;
      errors_.resize(saved_errors);
    }
    break;
  }
  if (operations.empty())
    return base;
  SourceLocation end = end_of(previous());
  return make_node<Expression>(
      *arena_, {.start = base->span.start, .end = end},
      PostfixExpression{.base = base, .operations = std::move(operations)});
}

PostfixOperation Parser::parse_postfix_operation() {
  if (check(TokenKind::LParen))
    return parse_call_operation();
  if (check(TokenKind::Less))
    return parse_generic_application();
  if (check(TokenKind::Dot))
    return parse_field_operation();
  if (check(TokenKind::LBracket))
    return parse_index_operation();
  return parse_propagation_operation();
}

CallOperation Parser::parse_call_operation() {
  expect(TokenKind::LParen, "expected '('");
  std::vector<Argument> arguments;
  if (!check(TokenKind::RParen))
    arguments = parse_argument_list();
  expect(TokenKind::RParen, "expected ')' after arguments");
  return {.arguments = std::move(arguments)};
}

std::vector<Argument> Parser::parse_argument_list() {
  std::vector<Argument> arguments;
  arguments.push_back(parse_argument());
  while (match(TokenKind::Comma)) {
    if (check(TokenKind::RParen))
      break;
    arguments.push_back(parse_argument());
  }
  return arguments;
}

Argument Parser::parse_argument() {
  Argument argument;
  if (check(TokenKind::Identifier) && check(TokenKind::Colon, 1)) {
    argument.name = current();
    advance();
    advance();
  }
  argument.value = parse_nested_expression();
  return argument;
}

GenericApplication Parser::parse_generic_application() {
  return {.arguments = parse_meta_argument_list()};
}

FieldOperation Parser::parse_field_operation() {
  expect(TokenKind::Dot, "expected '.'");
  return {.field = take(TokenKind::Identifier, "expected field name")};
}

IndexOperation Parser::parse_index_operation() {
  expect(TokenKind::LBracket, "expected '['");
  ExprPtr index = parse_nested_expression();
  expect(TokenKind::RBracket, "expected ']' after index");
  return {.index = index};
}

PropagationOperation Parser::parse_propagation_operation() {
  expect(TokenKind::Question, "expected '?'");
  return {};
}

ExprPtr Parser::parse_primary_expression() {
  if (literal_kind(current().kind))
    return parse_literal();
  if (check(TokenKind::KwFn))
    return parse_lambda_expression();
  if (check(TokenKind::LBracket))
    return parse_array_expression();
  if (check(TokenKind::LParen)) {
    std::size_t index = cursor_ + 1;
    std::size_t parens = 0;
    bool comma = false;
    while (index < tokens_.size()) {
      if (tokens_[index].kind == TokenKind::LParen)
        ++parens;
      else if (tokens_[index].kind == TokenKind::RParen) {
        if (parens == 0)
          break;
        --parens;
      } else if (tokens_[index].kind == TokenKind::Comma && parens == 0) {
        comma = true;
        break;
      }
      ++index;
    }
    return comma ? parse_tuple_expression() : parse_parenthesized_expression();
  }
  if (check(TokenKind::Identifier))
    return parse_identifier_expression();
  report_error(current(), "expected an expression");
  return nullptr;
}

ExprPtr Parser::parse_identifier_expression() {
  const Token start = current();
  QualifiedName name = parse_qualified_name();
  std::vector<MetaArgument> arguments;
  if (!stop_before_block_ && check(TokenKind::Less)) {
    const std::size_t saved_cursor = cursor_;
    const std::size_t saved_errors = errors_.size();
    arguments = parse_meta_argument_list();
    if (!check(TokenKind::LBrace) || errors_.size() != saved_errors) {
      cursor_ = saved_cursor;
      errors_.resize(saved_errors);
      arguments.clear();
    }
  }
  if (!stop_before_block_ && check(TokenKind::LBrace)) {
    std::vector<RecordInitializer> fields;
    advance();
    if (!check(TokenKind::RBrace))
      fields = parse_record_initializer_list();
    if (!expect(TokenKind::RBrace, "expected '}' after record initializer"))
      return nullptr;
    return make_node<Expression>(
        *arena_, span_from(start),
        RecordExpression{.name = std::move(name),
                         .arguments = std::move(arguments),
                         .fields = std::move(fields)});
  }
  return make_node<Expression>(*arena_, span_from(start),
                               NameExpression{.name = std::move(name)});
}

ExprPtr Parser::parse_parenthesized_expression() {
  const Token start = current();
  advance();
  ExprPtr expression = parse_nested_expression();
  if (!expression ||
      !expect(TokenKind::RParen, "expected ')' after expression"))
    return nullptr;
  return make_node<Expression>(
      *arena_, span_from(start),
      ParenthesizedExpression{.expression = expression});
}

ExprPtr Parser::parse_tuple_expression() {
  const Token start = current();
  advance();
  std::vector<ExprPtr> elements;
  ExprPtr first = parse_expr();
  if (!first)
    return nullptr;
  elements.push_back(first);
  if (!expect(TokenKind::Comma, "tuple requires at least two elements"))
    return nullptr;
  ExprPtr second = parse_expr();
  if (!second)
    return nullptr;
  elements.push_back(second);
  while (match(TokenKind::Comma)) {
    ExprPtr element = parse_expr();
    if (!element)
      return nullptr;
    elements.push_back(element);
  }
  if (!expect(TokenKind::RParen, "expected ')' after tuple"))
    return nullptr;
  return make_node<Expression>(
      *arena_, span_from(start),
      TupleExpression{.elements = std::move(elements)});
}

ExprPtr Parser::parse_array_expression() {
  const Token start = current();
  advance();
  std::vector<ExprPtr> elements;
  if (!check(TokenKind::RBracket))
    elements = parse_expression_list();
  if (!expect(TokenKind::RBracket, "expected ']' after array"))
    return nullptr;
  return make_node<Expression>(
      *arena_, span_from(start),
      ArrayExpression{.elements = std::move(elements)});
}

std::vector<ExprPtr> Parser::parse_expression_list() {
  std::vector<ExprPtr> expressions;
  if (ExprPtr first = parse_nested_expression())
    expressions.push_back(first);
  while (match(TokenKind::Comma)) {
    if (check(TokenKind::RBracket))
      break;
    if (ExprPtr next = parse_nested_expression())
      expressions.push_back(next);
  }
  return expressions;
}

ExprPtr Parser::parse_record_expression() {
  return parse_identifier_expression();
}

std::vector<RecordInitializer> Parser::parse_record_initializer_list() {
  std::vector<RecordInitializer> fields;
  fields.push_back(parse_record_initializer());
  while (match(TokenKind::Comma)) {
    if (check(TokenKind::RBrace))
      break;
    fields.push_back(parse_record_initializer());
  }
  return fields;
}

RecordInitializer Parser::parse_record_initializer() {
  Token name = take(TokenKind::Identifier, "expected record field name");
  ExprPtr value = nullptr;
  if (match(TokenKind::Colon))
    value = parse_nested_expression();
  else {
    QualifiedName qualified{.parts = {name}};
    value = make_node<Expression>(*arena_,
                                  {.start = name.location, .end = end_of(name)},
                                  NameExpression{.name = std::move(qualified)});
  }
  return {.name = std::move(name), .value = value};
}

ExprPtr Parser::parse_record_update_expression() {
  ExprPtr base = parse_postfix_expression_base();
  if (!base || !expect(TokenKind::KwWith, "expected 'with'"))
    return nullptr;
  const SourceLocation start = base->span.start;
  if (!expect(TokenKind::LBrace, "expected '{' after 'with'"))
    return nullptr;
  auto updates = parse_record_update_list();
  if (!expect(TokenKind::RBrace, "expected '}' after record updates"))
    return nullptr;
  return make_node<Expression>(
      *arena_, {.start = start, .end = end_of(previous())},
      RecordUpdateExpression{.base = base, .updates = std::move(updates)});
}

ExprPtr Parser::parse_postfix_expression_base() {
  ExprPtr base = parse_primary_expression();
  if (!base)
    return nullptr;
  std::vector<PostfixOperation> operations;
  while (check(TokenKind::LParen) || check(TokenKind::Dot) ||
         check(TokenKind::LBracket) || check(TokenKind::Question))
    operations.push_back(parse_postfix_operation());
  if (!operations.empty())
    base = make_node<Expression>(
        *arena_, {.start = base->span.start, .end = end_of(previous())},
        PostfixExpression{.base = base, .operations = std::move(operations)});
  if (check(TokenKind::KwWith)) {
    const SourceLocation start = base->span.start;
    advance();
    if (!expect(TokenKind::LBrace, "expected '{' after 'with'"))
      return nullptr;
    auto updates = parse_record_update_list();
    if (!expect(TokenKind::RBrace, "expected '}' after record updates"))
      return nullptr;
    return make_node<Expression>(
        *arena_, {.start = start, .end = end_of(previous())},
        RecordUpdateExpression{.base = base, .updates = std::move(updates)});
  }
  return base;
}

std::vector<RecordUpdate> Parser::parse_record_update_list() {
  std::vector<RecordUpdate> updates;
  if (check(TokenKind::RBrace)) {
    report_error(current(), "record update requires at least one field");
    return updates;
  }
  updates.push_back(parse_record_update());
  while (match(TokenKind::Comma)) {
    if (check(TokenKind::RBrace))
      break;
    updates.push_back(parse_record_update());
  }
  return updates;
}

RecordUpdate Parser::parse_record_update() {
  Token field = take(TokenKind::Identifier, "expected record field name");
  expect(TokenKind::Equal, "expected '=' in record update");
  return {.field = std::move(field), .value = parse_nested_expression()};
}

ExprPtr Parser::parse_lambda_expression() {
  const Token start = current();
  advance();
  if (!expect(TokenKind::LParen, "expected '(' after 'fn'"))
    return nullptr;
  std::vector<LambdaParameter> parameters;
  if (!check(TokenKind::RParen))
    parameters = parse_lambda_parameter_list();
  if (!expect(TokenKind::RParen, "expected ')' after lambda parameters"))
    return nullptr;
  TypePtr return_type = nullptr;
  if (match(TokenKind::Arrow))
    return_type = parse_type_expression();
  BlockPtr body = parse_block();
  if (!body)
    return nullptr;
  return make_node<Expression>(
      *arena_, {.start = start.location, .end = body->span.end},
      LambdaExpression{.parameters = std::move(parameters),
                       .return_type = return_type,
                       .body = body});
}

std::vector<LambdaParameter> Parser::parse_lambda_parameter_list() {
  std::vector<LambdaParameter> parameters;
  parameters.push_back(parse_lambda_parameter());
  while (match(TokenKind::Comma))
    parameters.push_back(parse_lambda_parameter());
  return parameters;
}

LambdaParameter Parser::parse_lambda_parameter() {
  LambdaParameter parameter{
      .name = take(TokenKind::Identifier, "expected lambda parameter")};
  if (match(TokenKind::Colon))
    parameter.type = parse_type_expression();
  return parameter;
}

PatternPtr Parser::parse_pattern() {
  if (check(TokenKind::Identifier) && current().lexeme == "_")
    return parse_wildcard_pattern();
  if (literal_kind(current().kind))
    return parse_literal_pattern();
  if (check(TokenKind::LParen))
    return parse_tuple_pattern();
  if (check(TokenKind::KwMut))
    return parse_binding_pattern();
  if (check(TokenKind::Identifier)) {
    bool structured = false;
    std::size_t index = cursor_ + 1;
    while (index + 1 < tokens_.size() &&
           tokens_[index].kind == TokenKind::Dot &&
           tokens_[index + 1].kind == TokenKind::Identifier)
      index += 2;
    structured = tokens_[index].kind == TokenKind::LParen ||
                 tokens_[index].kind == TokenKind::LBrace ||
                 index != cursor_ + 1;
    return structured ? parse_variant_pattern() : parse_binding_pattern();
  }
  report_error(current(), "expected a pattern");
  return nullptr;
}

PatternPtr Parser::parse_wildcard_pattern() {
  Token token = current();
  advance();
  return make_node<Pattern>(*arena_,
                            {.start = token.location, .end = end_of(token)},
                            WildcardPattern{});
}

PatternPtr Parser::parse_binding_pattern() {
  const Token start = current();
  const bool is_mutable = match(TokenKind::KwMut);
  Token name = take(TokenKind::Identifier, "expected binding name");
  return make_node<Pattern>(
      *arena_, span_from(start),
      BindingPattern{.is_mutable = is_mutable, .name = std::move(name)});
}

PatternPtr Parser::parse_literal_pattern() {
  Token token = current();
  advance();
  return make_node<Pattern>(*arena_,
                            {.start = token.location, .end = end_of(token)},
                            LiteralPattern{.literal = std::move(token)});
}

PatternPtr Parser::parse_tuple_pattern() {
  const Token start = current();
  advance();
  std::vector<PatternPtr> elements;
  PatternPtr first = parse_pattern();
  if (!first)
    return nullptr;
  elements.push_back(first);
  if (!expect(TokenKind::Comma, "tuple pattern requires at least two elements"))
    return nullptr;
  PatternPtr second = parse_pattern();
  if (!second)
    return nullptr;
  elements.push_back(second);
  while (match(TokenKind::Comma))
    elements.push_back(parse_pattern());
  if (!expect(TokenKind::RParen, "expected ')' after tuple pattern"))
    return nullptr;
  return make_node<Pattern>(*arena_, span_from(start),
                            TuplePattern{.elements = std::move(elements)});
}

PatternPtr Parser::parse_variant_pattern() {
  const Token start = current();
  QualifiedName name = parse_qualified_name();
  if (check(TokenKind::LBrace)) {
    advance();
    std::vector<RecordPatternField> fields;
    if (!check(TokenKind::RBrace))
      fields = parse_record_pattern_field_list();
    if (!expect(TokenKind::RBrace, "expected '}' after record pattern"))
      return nullptr;
    return make_node<Pattern>(
        *arena_, span_from(start),
        RecordPattern{.name = std::move(name), .fields = std::move(fields)});
  }
  std::optional<std::vector<PatternPtr>> arguments;
  if (match(TokenKind::LParen)) {
    arguments.emplace();
    if (!check(TokenKind::RParen))
      *arguments = parse_pattern_list();
    if (!expect(TokenKind::RParen, "expected ')' after variant pattern"))
      return nullptr;
  }
  return make_node<Pattern>(*arena_, span_from(start),
                            VariantPattern{.name = std::move(name),
                                           .arguments = std::move(arguments)});
}

std::vector<PatternPtr> Parser::parse_pattern_list() {
  std::vector<PatternPtr> patterns;
  patterns.push_back(parse_pattern());
  while (match(TokenKind::Comma))
    patterns.push_back(parse_pattern());
  return patterns;
}

PatternPtr Parser::parse_record_pattern() { return parse_variant_pattern(); }

std::vector<RecordPatternField> Parser::parse_record_pattern_field_list() {
  std::vector<RecordPatternField> fields;
  fields.push_back(parse_record_pattern_field());
  while (match(TokenKind::Comma)) {
    if (check(TokenKind::RBrace))
      break;
    fields.push_back(parse_record_pattern_field());
  }
  return fields;
}

RecordPatternField Parser::parse_record_pattern_field() {
  if (match(TokenKind::DotDot))
    return RestRecordPatternField{};
  Token name = take(TokenKind::Identifier, "expected record pattern field");
  if (match(TokenKind::Colon))
    return NamedRecordPatternField{.name = std::move(name),
                                   .pattern = parse_pattern()};
  return ShorthandRecordPatternField{.name = std::move(name)};
}

} // namespace flux::parser

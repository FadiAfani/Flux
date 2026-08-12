#pragma once

#include "../lexer/token.hpp"
#include "allocator.hpp"
#include "node.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace flux::parser {

struct ParseError {
  std::string message;
  SourceSpan loc;
};

struct ParseResult {
  std::shared_ptr<BumpAllocator> arena;
  Program *root = nullptr;
  std::vector<ParseError> errors;
};

class Parser {

private:
  std::vector<Token> tokens_;
  std::vector<ParseError> errors_;
  std::shared_ptr<BumpAllocator> arena_;
  std::size_t cursor_ = 0;

  void report_error(SourceSpan loc, std::string message);
  void report_error_with_span(const Token &start, const Token &end,
                              std::string message);
  void report_error(const Token &start, std::string message);
  std::optional<FunctionSignature> parse_function_signature();

public:
  explicit Parser(std::vector<Token> tokens);

  ParseResult parse();
  Expr *parse_literal();
  Expr *parse_unary_expr();
  Expr *parse_expr();

  Program *parse_source_file();
  ModuleDeclaration *parse_module_declaration();
  ImportDeclaration *parse_import_declaration();
  std::optional<ImportSelector> parse_import_selector();
  std::vector<ImportItem> parse_import_item_list();
  ImportItem parse_import_item();
  QualifiedName parse_qualified_name();
  std::optional<TopLevelDeclaration> parse_top_level_declaration();
  bool parse_visibility();

  ConstantDeclaration *parse_const_declaration();
  FunctionDeclaration *parse_function_declaration();
  ExternalFunctionDeclaration *parse_external_function_declaration();
  std::vector<Parameter> parse_parameter_list();
  Parameter parse_parameter();
  std::pair<bool, TypePtr> parse_parameter_type();
  FunctionClause parse_function_clause();
  RequiresClause parse_requires_clause();
  EnsuresClause parse_ensures_clause();
  UsesClause parse_uses_clause();
  std::vector<EffectReference> parse_effect_reference_list();
  EffectReference parse_effect_reference();

  std::vector<GenericParameter> parse_generic_parameter_list();
  GenericParameter parse_generic_parameter();
  std::variant<KindPtr, TypePtr> parse_generic_domain();
  KindPtr parse_kind_expression();
  KindPtr parse_kind_arrow_expression();
  KindPtr parse_kind_primary();
  std::vector<MetaArgument> parse_meta_argument_list();
  MetaArgument parse_meta_argument();
  Expr *parse_constant_meta_expression();

  std::optional<TypeDeclaration> parse_type_declaration();
  TypeAliasDeclaration *parse_type_alias_declaration();
  PredicatePtr parse_refinement_clause();
  RecordTypeDeclaration *parse_record_type_declaration();
  std::optional<RecordMember> parse_record_member();
  FieldDeclaration parse_field_declaration();
  InvariantDeclaration *parse_invariant_declaration();
  SumTypeDeclaration *parse_sum_type_declaration();
  VariantDeclaration parse_variant_declaration();
  std::vector<VariantField> parse_variant_field_list();
  VariantField parse_variant_field();

  TypePtr parse_type_expression();
  TypePtr parse_union_type();
  TypePtr parse_function_type();
  TypePtr parse_function_type_expression();
  std::vector<EffectReference> parse_function_type_effects();
  std::vector<TypePtr> parse_type_expression_list();
  TypePtr parse_type_postfix_expression();
  std::vector<MetaArgument> parse_type_postfix();
  TypePtr parse_type_primary();
  TypePtr parse_tuple_type();
  TypePtr parse_structural_record_type();
  std::vector<RowField> parse_row_field_list();
  RowField parse_row_field();
  Token parse_row_tail();

  TraitDeclaration *parse_trait_declaration();
  std::optional<TraitMember> parse_trait_member();
  TraitFunctionDeclaration parse_trait_function_declaration();
  LawDeclaration *parse_law_declaration();
  ImplDeclaration *parse_impl_declaration();
  TraitReference parse_trait_reference();
  FunctionDeclaration *parse_impl_member();
  EffectDeclaration *parse_effect_declaration();
  CapabilityDeclaration parse_capability_declaration();
  DomainDeclaration *parse_domain_declaration();
  std::optional<DomainMember> parse_domain_member();

  BlockPtr parse_block();
  StmtPtr parse_statement();
  StmtPtr parse_let_statement();
  StmtPtr parse_var_statement();
  StmtPtr parse_assignment_statement();
  Token parse_assignment_operator();
  LValue parse_lvalue();
  LValuePostfix parse_lvalue_postfix();
  StmtPtr parse_return_statement();
  StmtPtr parse_break_statement();
  StmtPtr parse_continue_statement();
  StmtPtr parse_while_statement();
  StmtPtr parse_for_statement();
  StmtPtr parse_mutate_statement();
  StmtPtr parse_transaction_statement();
  StmtPtr parse_parallel_statement();
  StmtPtr parse_unsafe_statement();
  StmtPtr parse_expression_statement();
  ExprPtr parse_tail_expression();

  ExprPtr parse_if_expression();
  ExprPtr parse_match_expression();
  MatchArm parse_match_arm();
  ExprPtr parse_logical_or_expression();
  ExprPtr parse_logical_and_expression();
  ExprPtr parse_equality_expression();
  Token parse_equality_operator();
  ExprPtr parse_comparison_expression();
  Token parse_comparison_operator();
  ExprPtr parse_range_expression();
  Token parse_range_operator();
  ExprPtr parse_additive_expression();
  Token parse_additive_operator();
  ExprPtr parse_multiplicative_expression();
  Token parse_multiplicative_operator();
  Token parse_unary_operator();
  ExprPtr parse_postfix_expression();
  PostfixOperation parse_postfix_operation();
  CallOperation parse_call_operation();
  std::vector<Argument> parse_argument_list();
  Argument parse_argument();
  GenericApplication parse_generic_application();
  FieldOperation parse_field_operation();
  IndexOperation parse_index_operation();
  PropagationOperation parse_propagation_operation();
  ExprPtr parse_primary_expression();
  ExprPtr parse_identifier_expression();
  ExprPtr parse_parenthesized_expression();
  ExprPtr parse_tuple_expression();
  ExprPtr parse_array_expression();
  std::vector<ExprPtr> parse_expression_list();
  ExprPtr parse_record_expression();
  std::vector<RecordInitializer> parse_record_initializer_list();
  RecordInitializer parse_record_initializer();
  ExprPtr parse_record_update_expression();
  ExprPtr parse_postfix_expression_base();
  std::vector<RecordUpdate> parse_record_update_list();
  RecordUpdate parse_record_update();
  ExprPtr parse_lambda_expression();
  std::vector<LambdaParameter> parse_lambda_parameter_list();
  LambdaParameter parse_lambda_parameter();

  PatternPtr parse_pattern();
  PatternPtr parse_wildcard_pattern();
  PatternPtr parse_binding_pattern();
  PatternPtr parse_literal_pattern();
  PatternPtr parse_tuple_pattern();
  PatternPtr parse_variant_pattern();
  std::vector<PatternPtr> parse_pattern_list();
  PatternPtr parse_record_pattern();
  std::vector<RecordPatternField> parse_record_pattern_field_list();
  RecordPatternField parse_record_pattern_field();

  PredicatePtr parse_predicate();
  PredicatePtr parse_predicate_implication();
  PredicatePtr parse_predicate_equivalence();
  PredicatePtr parse_predicate_or();
  PredicatePtr parse_predicate_and();
  PredicatePtr parse_predicate_unary();
  PredicatePtr parse_quantified_predicate();
  Quantifier parse_quantifier();
  std::vector<QuantifierBinding> parse_quantifier_binding_list();
  QuantifierBinding parse_quantifier_binding();
  std::variant<TypePtr, KindPtr> parse_quantifier_domain();
  PredicatePtr parse_predicate_atom();
  PredicatePtr parse_predicate_comparison();
  Token parse_predicate_comparison_operator();
  PredicatePtr parse_predicate_call();

  SpecExprPtr parse_spec_expression();
  SpecExprPtr parse_spec_additive();
  SpecExprPtr parse_spec_multiplicative();
  SpecExprPtr parse_spec_unary();
  SpecExprPtr parse_spec_postfix();
  SpecExprPtr parse_spec_primary();
  std::vector<SpecExprPtr> parse_spec_expression_list();
  SpecExprPtr parse_old_expression();

  Token parse_boolean_literal();
  Token parse_integer_literal();
  Token parse_floating_literal();
  Token parse_character_literal();
  Token parse_string_literal();
  Token parse_identifier();

  const Token &peek();
  const Token &peek_next();
  SourceSpan get_node_span();
  void advance();
};

} // namespace flux::parser

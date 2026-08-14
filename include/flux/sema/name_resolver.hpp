#pragma once

#include "flux/parser/allocator.hpp"
#include "flux/parser/node.hpp"
#include "flux/sema/semantic_context.hpp"
#include "flux/sema/symbol.hpp"

#include <stack>
#include <string>
#include <string_view>
#include <vector>

namespace flux::semantic_analysis {

class NameResolver {
public:
  class NameInterner {
  public:
    NameId intern(std::string_view name);
    NameId intern(const flux::parser::QualifiedName &name);
    std::string_view get(NameId id) const;
    std::size_t size() const noexcept;

  private:
    std::vector<std::string> names_;
  };

  NameResolver(SemanticContext &context,
               flux::parser::BumpAllocator &hir_arena);

  void resolve(const flux::parser::Program &program);

  NameInterner &names() noexcept;
  const NameInterner &names() const noexcept;

private:
  void declare_module(const flux::parser::ModuleDeclaration &declaration);
  void resolve_import(const flux::parser::ImportDeclaration &declaration);
  void declare_top_level_names(const flux::parser::Program &program);
  void declare_top_level(const flux::parser::TopLevelDeclaration &declaration);
  void resolve_top_level_declarations(const flux::parser::Program &program);
  void resolve_top_level(const flux::parser::TopLevelDeclaration &declaration);

  void resolve_function(const flux::parser::FunctionDeclaration &declaration);
  void resolve_external_function(
      const flux::parser::ExternalFunctionDeclaration &declaration);
  void
  resolve_function_signature(const flux::parser::FunctionSignature &signature);
  void resolve_function_clause(const flux::parser::FunctionClause &clause);
  void resolve_constant(const flux::parser::ConstantDeclaration &declaration);
  void
  resolve_type_declaration(const flux::parser::TypeDeclaration &declaration);
  void
  resolve_type_alias(const flux::parser::TypeAliasDeclaration &declaration);
  void
  resolve_record_type(const flux::parser::RecordTypeDeclaration &declaration);
  void resolve_sum_type(const flux::parser::SumTypeDeclaration &declaration);
  void resolve_trait(const flux::parser::TraitDeclaration &declaration);
  void resolve_impl(const flux::parser::ImplDeclaration &declaration);
  void resolve_effect(const flux::parser::EffectDeclaration &declaration);
  void resolve_domain(const flux::parser::DomainDeclaration &declaration);

  void resolve_generic_parameters(
      const std::vector<flux::parser::GenericParameter> &parameters);
  void resolve_type(flux::parser::TypePtr type);
  void resolve_kind(flux::parser::KindPtr kind);
  void resolve_meta_argument(const flux::parser::MetaArgument &argument);
  void resolve_effect_reference(const flux::parser::EffectReference &reference);

  void resolve_block(flux::parser::BlockPtr block);
  void resolve_statement(flux::parser::StmtPtr statement);
  void resolve_expression(flux::parser::ExprPtr expression);
  void resolve_pattern(flux::parser::PatternPtr pattern, bool declares_names);
  void resolve_predicate(flux::parser::PredicatePtr predicate);
  void resolve_spec_expression(flux::parser::SpecExprPtr expression);

  void resolve_qualified_name(flux::parser::QualifiedName name);

  [[maybe_unused]] SemanticContext &context_;
  [[maybe_unused]] flux::parser::BumpAllocator &hir_arena_;
  NameInterner names_;
  std::optional<ModuleId> current_module_;
  std::stack<ScopeId> scopes_;
};

} // namespace flux::semantic_analysis

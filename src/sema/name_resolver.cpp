#include "flux/sema/name_resolver.hpp"
#include "flux/parser/node.hpp"
#include "flux/sema/error.hpp"
#include "flux/sema/symbol.hpp"
#include "flux/support/overloaded.hpp"

#include <algorithm>
#include <ios>
#include <limits>
#include <stdexcept>
#include <string>
#include <variant>

namespace flux::semantic_analysis {
namespace {

SourceSpan token_span(const Token &token) {
  SourceLocation end = token.location;
  end.offset += token.lexeme.size();
  end.column += token.lexeme.size();
  return {.start = token.location, .end = end};
}

} // namespace

NameId NameResolver::NameInterner::intern(std::string_view name) {
  for (std::size_t index = 0; index < names_.size(); ++index) {
    if (names_[index] == name) {
      return {.value = static_cast<std::uint32_t>(index)};
    }
  }
  if (names_.size() > std::numeric_limits<std::uint32_t>::max()) {
    throw std::length_error("name interner exhausted its ID space");
  }

  const NameId id{.value = static_cast<std::uint32_t>(names_.size())};
  names_.emplace_back(name);
  return id;
}

NameId NameResolver::NameInterner::intern(const parser::QualifiedName &name) {
  if (name.parts.empty()) {
    throw std::invalid_argument("qualified name cannot be empty");
  }

  std::string qualified_name;
  for (const Token &part : name.parts) {
    if (!qualified_name.empty()) {
      qualified_name.push_back('.');
    }
    qualified_name += part.lexeme;
  }
  return intern(qualified_name);
}

std::string_view NameResolver::NameInterner::get(NameId id) const {
  return names_.at(id.value);
}

std::size_t NameResolver::NameInterner::size() const noexcept {
  return names_.size();
}

NameResolver::NameResolver(SemanticContext &context,
                           parser::BumpAllocator &hir_arena)
    : context_(context), hir_arena_(hir_arena) {}

void NameResolver::resolve(const parser::Program &program) {
  current_module_.reset();
  if (!program.module) {
    context_.report_error(SemanticErrorCode::MissingModuleDeclaration,
                          program.span,
                          "source file requires a module declaration");
    return;
  }

  declare_top_level_names(program);
  resolve_top_level_declarations(program);
}

NameResolver::NameInterner &NameResolver::names() noexcept { return names_; }

const NameResolver::NameInterner &NameResolver::names() const noexcept {
  return names_;
}

void NameResolver::declare_module(
    const parser::ModuleDeclaration &declaration) {
  const NameId name = names_.intern(declaration.name);
  current_module_ = context_.modules.register_module(name);
  Module &module = context_.modules.get_mut(*current_module_);
  if (!module.root_scope) {
    module.root_scope = {
        .value = static_cast<std::uint32_t>(context_.scopes.size())};
    context_.scopes.push_back({.parent = std::nullopt});
  }
}

void NameResolver::resolve_import(
    const parser::ImportDeclaration &declaration) {
  if (!current_module_.has_value()) {
    throw std::logic_error("no module was found ! cannot resolve imports");
  }

  const NameId imported_name = names_.intern(declaration.name);
  const ModuleId imported = context_.modules.register_module(imported_name);

  Module &module = context_.modules.get_mut(*current_module_);
  const auto add_import = [&] {
    if (std::find(module.imports.begin(), module.imports.end(), imported) ==
        module.imports.end()) {
      module.imports.push_back(imported);
    }
  };

  if (!declaration.selector) {
    add_import();
    return;
  }

  std::visit(support::Overloaded{
                 [&](const parser::ImportAll &) { add_import(); },
                 [&](const parser::ImportAlias &alias) {
                   names_.intern(alias.alias.lexeme);
                   add_import();
                 },
                 [&](const parser::ImportItems &items) {
                   for (const parser::ImportItem &item : items.items) {
                     names_.intern(item.name.lexeme);
                     if (item.alias) {
                       names_.intern(item.alias->lexeme);
                     }
                   }
                   add_import();
                 },
             },
             *declaration.selector);
}

void NameResolver::declare_top_level_names(const parser::Program &program) {
  declare_module(*program.module);

  Scope &scope = context_.scopes.at(
      context_.modules.get(*current_module_).root_scope->value);
  for (const parser::ImportDeclaration *declaration : program.imports) {
    if (!declaration || declaration->name.parts.empty()) {
      continue;
    }

    const NameId imported_name = names_.intern(declaration->name);
    const ModuleId imported = context_.modules.register_module(imported_name);
    const auto bind = [&](const Token &token, BindingTarget target,
                          BindingOrigin origin) {
      scope.bindings.emplace(names_.intern(token.lexeme),
                             Binding{.target = std::move(target),
                                     .origin = origin,
                                     .defined_at = token_span(token)});
    };

    // we're keeping a flat binding design for now - only last name is bound to
    // the actual model it references
    if (!declaration->selector) {
      bind(declaration->name.parts.back(), imported,
           BindingOrigin::ModuleImport);
      continue;
    }

    std::visit(
        support::Overloaded{
            [&](const parser::ImportAll &) {},
            [&](const parser::ImportAlias &alias) {
              bind(alias.alias, imported, BindingOrigin::ImportAlias);
            },
            [&](const parser::ImportItems &items) {
              for (const parser::ImportItem &item : items.items) {
                const Token &local_name = item.alias ? *item.alias : item.name;
                bind(local_name,
                     ImportedSymbol{.module = imported,
                                    .name = names_.intern(item.name.lexeme)},
                     BindingOrigin::SelectedImport);
              }
            },
        },
        *declaration->selector);
  }

  for (auto &decl : program.declarations) {
    declare_top_level(decl);
  }
}

void NameResolver::declare_top_level(
    const parser::TopLevelDeclaration &declaration) {
  if (!current_module_.has_value())
    throw std::logic_error("no module is declared");

  Module &module = context_.modules.get_mut(*current_module_);
  if (!module.root_scope)
    throw std::logic_error("module has no root scope");

  const ScopeId root_scope = *module.root_scope;
  Scope &scope = context_.scopes.at(root_scope.value);
  const auto declare = [&](const Token &token, SymbolKind kind,
                           SourceSpan declaration_span, bool is_public) {
    const NameId name = names_.intern(token.lexeme);
    const SymbolId symbol = module.symbols.add({
        .owner_scope = root_scope,
        .kind = kind,
        .name = name,
        .declaration_span = declaration_span,
        .is_public = is_public,
    });
    scope.bindings.emplace(name, Binding{.target = symbol,
                                         .origin = BindingOrigin::Declaration,
                                         .defined_at = token_span(token)});
  };

  std::visit(
      support::Overloaded{
          [&](const parser::FunctionDeclaration *decl) {
            declare(decl->signature.name, SymbolKind::Function, decl->span,
                    decl->is_public);
          },
          [&](const parser::ConstantDeclaration *decl) {
            declare(decl->name, SymbolKind::Constant, decl->span,
                    decl->is_public);
          },
          [&](const parser::ExternalFunctionDeclaration *decl) {
            declare(decl->signature.name, SymbolKind::ExternalFunction,
                    decl->span, decl->is_public);
          },
          [&](const parser::TypeAliasDeclaration *decl) {
            declare(decl->name, SymbolKind::Type, decl->span, decl->is_public);
          },
          [&](const parser::RecordTypeDeclaration *decl) {
            declare(decl->name, SymbolKind::Type, decl->span, decl->is_public);
          },
          [&](const parser::SumTypeDeclaration *decl) {
            declare(decl->name, SymbolKind::Type, decl->span, decl->is_public);
          },
          [&](const parser::TraitDeclaration *decl) {
            declare(decl->name, SymbolKind::Trait, decl->span, decl->is_public);
          },
          [&](const parser::ImplDeclaration *) {},
          [&](const parser::EffectDeclaration *decl) {
            declare(decl->name, SymbolKind::Effect, decl->span,
                    decl->is_public);
          },
          [&](const parser::DomainDeclaration *decl) {
            declare(decl->name, SymbolKind::Domain, decl->span,
                    decl->is_public);
          },
      },
      declaration);
}

void NameResolver::resolve_top_level_declarations(
    const parser::Program &program) {

  if (!program.module) {
    context_.report_error(SemanticErrorCode::MissingModuleDeclaration,
                          program.span, "Missing Module Declaration");
  }

  for (auto &decl : program.declarations) {
    resolve_top_level(decl);
  }

  for (auto import : program.imports) {
    if (import)
      resolve_import(*import);
  }
}

void NameResolver::resolve_top_level(
    const parser::TopLevelDeclaration &declaration) {

  std::visit(
      support::Overloaded{
          [&](const parser::FunctionDeclaration *decl) {
            resolve_function(*decl);
          },
          [&](const parser::ConstantDeclaration *decl) {
            resolve_constant(*decl);
          },
          [&](const parser::ExternalFunctionDeclaration *decl) {
            resolve_external_function(*decl);
          },
          [&](const parser::TypeAliasDeclaration *decl) {
            resolve_type_alias(*decl);
          },
          [&](const parser::RecordTypeDeclaration *decl) {
            resolve_record_type(*decl);
          },
          [&](const parser::SumTypeDeclaration *decl) {
            resolve_sum_type(*decl);
          },
          [&](const parser::TraitDeclaration *decl) { resolve_trait(*decl); },
          [&](const parser::ImplDeclaration *decl) { resolve_impl(*decl); },
          [&](const parser::EffectDeclaration *decl) { resolve_effect(*decl); },
          [&](const parser::DomainDeclaration *decl) { resolve_domain(*decl); },
      },
      declaration);
}

void NameResolver::resolve_function(
    const parser::FunctionDeclaration &declaration) {
  resolve_function_signature(declaration.signature);
  resolve_block(declaration.body);
}

void NameResolver::resolve_external_function(
    const parser::ExternalFunctionDeclaration &declaration) {
  resolve_function_signature(declaration.signature);
}

void NameResolver::resolve_function_signature(
    const parser::FunctionSignature &signature) {
  resolve_generic_parameters(signature.generic_parameters);
  for (const parser::Parameter &parameter : signature.parameters) {
    resolve_type(parameter.type);
  }
  resolve_type(signature.return_type);
  for (const parser::FunctionClause &clause : signature.clauses) {
    resolve_function_clause(clause);
  }
}

void NameResolver::resolve_function_clause(
    const parser::FunctionClause &clause) {
  std::visit(support::Overloaded{
                 [&](const parser::RequiresClause &requirement) {
                   resolve_predicate(requirement.predicate);
                 },
                 [&](const parser::EnsuresClause &ensures) {
                   resolve_predicate(ensures.predicate);
                 },
                 [&](const parser::UsesClause &uses) {
                   for (const parser::EffectReference &effect : uses.effects) {
                     resolve_effect_reference(effect);
                   }
                 },
             },
             clause);
}

void NameResolver::resolve_constant(
    const parser::ConstantDeclaration &declaration) {
  resolve_type(declaration.type);
  resolve_expression(declaration.value);
}

void NameResolver::resolve_type_declaration(
    const parser::TypeDeclaration &declaration) {
  std::visit(support::Overloaded{
                 [&](const parser::TypeAliasDeclaration *decl) {
                   resolve_type_alias(*decl);
                 },
                 [&](const parser::RecordTypeDeclaration *decl) {
                   resolve_record_type(*decl);
                 },
                 [&](const parser::SumTypeDeclaration *decl) {
                   resolve_sum_type(*decl);
                 },
             },
             declaration);
}

void NameResolver::resolve_type_alias(
    const parser::TypeAliasDeclaration &declaration) {
  resolve_generic_parameters(declaration.generic_parameters);
  resolve_type(declaration.type);
  resolve_predicate(declaration.refinement);
}

void NameResolver::resolve_record_type(
    const parser::RecordTypeDeclaration &declaration) {
  resolve_generic_parameters(declaration.generic_parameters);
  for (const parser::RecordMember &member : declaration.members) {
    std::visit(support::Overloaded{
                   [&](const parser::FieldDeclaration &field) {
                     resolve_type(field.type);
                   },
                   [&](const parser::InvariantDeclaration *invariant) {
                     resolve_predicate(invariant->predicate);
                   },
               },
               member);
  }
}

void NameResolver::resolve_sum_type(
    const parser::SumTypeDeclaration &declaration) {
  resolve_generic_parameters(declaration.generic_parameters);
  for (const parser::VariantDeclaration &variant : declaration.variants) {
    if (!variant.fields) {
      continue;
    }
    for (const parser::VariantField &field : *variant.fields) {
      resolve_type(field.type);
    }
  }
}

void NameResolver::resolve_trait(const parser::TraitDeclaration &declaration) {
  resolve_generic_parameters(declaration.generic_parameters);
  for (const parser::TraitMember &member : declaration.members) {
    std::visit(support::Overloaded{
                   [&](const parser::TraitFunctionDeclaration &function) {
                     resolve_function_signature(function.signature);
                   },
                   [&](const parser::LawDeclaration *law) {
                     resolve_predicate(law->predicate);
                   },
               },
               member);
  }
}

void NameResolver::resolve_impl(const parser::ImplDeclaration &declaration) {
  resolve_qualified_name(declaration.trait.name);
  for (const parser::MetaArgument &argument : declaration.trait.arguments) {
    resolve_meta_argument(argument);
  }
  resolve_type(declaration.target);
  for (const parser::FunctionDeclaration *function : declaration.functions) {
    resolve_function(*function);
  }
}

void NameResolver::resolve_effect(
    const parser::EffectDeclaration &declaration) {
  (void)declaration;
}

void NameResolver::resolve_domain(
    const parser::DomainDeclaration &declaration) {
  for (const parser::DomainMember &member : declaration.members) {
    std::visit(support::Overloaded{
                   [&](const parser::InvariantDeclaration *invariant) {
                     resolve_predicate(invariant->predicate);
                   },
                   [&](const parser::FunctionDeclaration *function) {
                     resolve_function(*function);
                   },
                   [&](const parser::TypeDeclaration &type) {
                     resolve_type_declaration(type);
                   },
               },
               member);
  }
}

void NameResolver::resolve_generic_parameters(
    const std::vector<parser::GenericParameter> &parameters) {
  for (const parser::GenericParameter &parameter : parameters) {
    std::visit(support::Overloaded{
                   [](std::monostate) {},
                   [&](parser::KindPtr kind) { resolve_kind(kind); },
                   [&](parser::TypePtr type) { resolve_type(type); },
               },
               parameter.domain);
  }
}

void NameResolver::resolve_type(parser::TypePtr type) {
  if (!type) {
    return;
  }

  std::visit(
      support::Overloaded{
          [&](const parser::TypeName &name) {
            resolve_qualified_name(name.name);
          },
          [&](const parser::UnionType &union_type) {
            for (parser::TypePtr member : union_type.members) {
              resolve_type(member);
            }
          },
          [&](const parser::FunctionType &function) {
            for (parser::TypePtr parameter : function.parameters) {
              resolve_type(parameter);
            }
            resolve_type(function.result);
            for (const parser::EffectReference &effect : function.effects) {
              resolve_effect_reference(effect);
            }
          },
          [&](const parser::AppliedType &applied) {
            resolve_type(applied.base);
            for (const parser::MetaArgument &argument : applied.arguments) {
              resolve_meta_argument(argument);
            }
          },
          [&](const parser::TupleType &tuple) {
            for (parser::TypePtr element : tuple.elements) {
              resolve_type(element);
            }
          },
          [&](const parser::StructuralRecordType &record) {
            for (const parser::RowField &field : record.fields) {
              resolve_type(field.type);
            }
            if (record.tail) {
              resolve_qualified_name({.parts = {*record.tail}});
            }
          },
          [&](const parser::ParenthesizedType &parenthesized) {
            resolve_type(parenthesized.type);
          },
      },
      type->value);
}

void NameResolver::resolve_kind(parser::KindPtr kind) {
  if (!kind) {
    return;
  }

  std::visit(support::Overloaded{
                 [](const parser::TypeKind &) {},
                 [&](const parser::ArrowKind &arrow) {
                   resolve_kind(arrow.parameter);
                   resolve_kind(arrow.result);
                 },
                 [&](const parser::ParenthesizedKind &parenthesized) {
                   resolve_kind(parenthesized.kind);
                 },
             },
             kind->value);
}

void NameResolver::resolve_meta_argument(const parser::MetaArgument &argument) {
  std::visit(
      support::Overloaded{
          [&](parser::TypePtr type) { resolve_type(type); },
          [&](parser::ExprPtr expression) { resolve_expression(expression); },
      },
      argument.value);
}

void NameResolver::resolve_effect_reference(
    const parser::EffectReference &reference) {
  resolve_qualified_name(reference.name);
  for (const parser::MetaArgument &argument : reference.arguments) {
    resolve_meta_argument(argument);
  }
}

void NameResolver::resolve_block(parser::BlockPtr block) {}

void NameResolver::resolve_statement(parser::StmtPtr statement) {
  (void)statement;
}

void NameResolver::resolve_expression(parser::ExprPtr expression) {
  (void)expression;
}

void NameResolver::resolve_pattern(parser::PatternPtr pattern,
                                   bool declares_names) {
  (void)pattern;
  (void)declares_names;
}

void NameResolver::resolve_predicate(parser::PredicatePtr predicate) {
  (void)predicate;
}

void NameResolver::resolve_spec_expression(parser::SpecExprPtr expression) {
  (void)expression;
}

void NameResolver::resolve_qualified_name(parser::QualifiedName name) {}

} // namespace flux::semantic_analysis

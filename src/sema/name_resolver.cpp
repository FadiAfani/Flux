#include "flux/sema/name_resolver.hpp"
#include "flux/parser/node.hpp"
#include "flux/sema/symbol.hpp"
#include "flux/support/overloaded.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
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
      return static_cast<NameId>(index);
    }
  }
  if (names_.size() > std::numeric_limits<NameId>::max()) {
    throw std::length_error("name interner exhausted its ID space");
  }

  const NameId id = static_cast<NameId>(names_.size());
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
  return names_.at(id);
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
    return;
  }

  declare_top_level_names(program);
  for (const parser::ImportDeclaration *import : program.imports) {
    if (import) {
      resolve_import(*import);
    }
  }
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
    module.root_scope = static_cast<ScopeId>(context_.scopes.size());
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
  if (!program.module) {
    throw std::logic_error("source file requires a module declaration");
  }
  declare_module(*program.module);

  Scope &scope =
      context_.scopes.at(*context_.modules.get(*current_module_).root_scope);
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

  std::visit(support::Overloaded{[&](const parser::FunctionDeclaration *decl) {
    NameId name = names_.intern(decl->signature.name.lexeme);
    if (!current_module_.has_value())
      throw std::logic_error("no module is decalred");

    auto &mod = context_.modules.get_mut(current_module_.value());

    Symbol sym = {
        .owner_scope = scopes_.top(),
        .kind = SymbolKind::Function,
        .name = name,
        .declaration_span = decl->span,
        .is_public = decl->is_public,
    };

    mod.symbols.add(sym);
  }});
}

void NameResolver::resolve_top_level_declarations(
    const parser::Program &program) {
  (void)program;
}

void NameResolver::resolve_top_level(
    const parser::TopLevelDeclaration &declaration) {
  (void)declaration;
}

void NameResolver::resolve_function(
    const parser::FunctionDeclaration &declaration) {
  (void)declaration;
}

void NameResolver::resolve_external_function(
    const parser::ExternalFunctionDeclaration &declaration) {
  (void)declaration;
}

void NameResolver::resolve_function_signature(
    const parser::FunctionSignature &signature) {
  (void)signature;
}

void NameResolver::resolve_function_clause(
    const parser::FunctionClause &clause) {
  (void)clause;
}

void NameResolver::resolve_constant(
    const parser::ConstantDeclaration &declaration) {
  (void)declaration;
}

void NameResolver::resolve_type_declaration(
    const parser::TypeDeclaration &declaration) {
  (void)declaration;
}

void NameResolver::resolve_type_alias(
    const parser::TypeAliasDeclaration &declaration) {
  (void)declaration;
}

void NameResolver::resolve_record_type(
    const parser::RecordTypeDeclaration &declaration) {
  (void)declaration;
}

void NameResolver::resolve_sum_type(
    const parser::SumTypeDeclaration &declaration) {
  (void)declaration;
}

void NameResolver::resolve_trait(const parser::TraitDeclaration &declaration) {
  (void)declaration;
}

void NameResolver::resolve_impl(const parser::ImplDeclaration &declaration) {
  (void)declaration;
}

void NameResolver::resolve_effect(
    const parser::EffectDeclaration &declaration) {
  (void)declaration;
}

void NameResolver::resolve_domain(
    const parser::DomainDeclaration &declaration) {
  (void)declaration;
}

void NameResolver::resolve_generic_parameters(
    const std::vector<parser::GenericParameter> &parameters) {
  (void)parameters;
}

void NameResolver::resolve_type(parser::TypePtr type) { (void)type; }

void NameResolver::resolve_kind(parser::KindPtr kind) { (void)kind; }

void NameResolver::resolve_meta_argument(const parser::MetaArgument &argument) {
  (void)argument;
}

void NameResolver::resolve_effect_reference(
    const parser::EffectReference &reference) {
  (void)reference;
}

void NameResolver::resolve_block(parser::BlockPtr block) { (void)block; }

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

void NameResolver::resolve_qualified_name(parser::QualifiedName name) {
  (void)name;
}

} // namespace flux::semantic_analysis

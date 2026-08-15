#include "flux/sema/name_resolver.hpp"

#include <cstdlib>
#include <string>
#include <string_view>

using flux::parser::BumpAllocator;
using flux::parser::Program;
using flux::semantic_analysis::BindingOrigin;
using flux::semantic_analysis::ImportedSymbol;
using flux::semantic_analysis::ModuleId;
using flux::semantic_analysis::NameResolver;
using flux::semantic_analysis::Scope;
using flux::semantic_analysis::SemanticContext;
using flux::semantic_analysis::SemanticErrorCode;
using flux::semantic_analysis::Symbol;
using flux::semantic_analysis::SymbolKind;
using flux::semantic_analysis::SymbolTable;

int main() {
  SymbolTable symbols;
  const auto symbol = symbols.add(Symbol{.owner_scope = {.value = 1},
                                         .kind = SymbolKind::Local,
                                         .name = {.value = 0},
                                         .declaration_span = {}});
  if (symbols.get(symbol).owner_scope !=
      flux::semantic_analysis::ScopeId{.value = 1}) {
    return EXIT_FAILURE;
  }

  SemanticContext context;
  const auto module = context.modules.register_module({.value = 7});
  if (module.value != 0 || context.modules.size() != 1 ||
      context.modules.get(module).interned_name !=
          flux::semantic_analysis::NameId{.value = 7} ||
      !context.modules.get(module).imports.empty() ||
      context.modules.get(module).symbols.size() != 0) {
    return EXIT_FAILURE;
  }

  context.scopes.push_back(Scope{.parent = std::nullopt});
  context.scopes.push_back(
      Scope{.parent = flux::semantic_analysis::ScopeId{.value = 0}});
  if (context.scopes[1].parent !=
      flux::semantic_analysis::ScopeId{.value = 0}) {
    return EXIT_FAILURE;
  }
  BumpAllocator arena;
  Program program;
  NameResolver resolver(context, arena);
  const auto first_name = resolver.names().intern("value");
  const auto same_name = resolver.names().intern("value");
  if (first_name != same_name || resolver.names().get(first_name) != "value" ||
      resolver.names().size() != 1) {
    return EXIT_FAILURE;
  }

  SemanticContext error_context;
  NameResolver error_resolver(error_context, arena);
  Program missing_module_program;
  missing_module_program.span = {
      .start = {.offset = 4, .line = 2, .column = 3},
      .end = {.offset = 12, .line = 2, .column = 11}};
  error_resolver.resolve(missing_module_program);
  if (error_context.errors.size() != 1 ||
      error_context.errors.front().code !=
          SemanticErrorCode::MissingModuleDeclaration ||
      error_context.errors.front().message !=
          "source file requires a module declaration" ||
      error_context.errors.front().span.start.offset != 4 ||
      error_context.errors.front().span.end.offset != 12 ||
      error_context.modules.size() != 0 || !error_context.scopes.empty()) {
    return EXIT_FAILURE;
  }

  flux::parser::ModuleDeclaration imported_module;
  imported_module.name.parts = {
      {.kind = flux::TokenKind::Identifier, .lexeme = "library"}};
  Program imported_program;
  imported_program.module = &imported_module;
  resolver.resolve(imported_program);
  const auto imported =
      context.modules.find(resolver.names().intern("library"));
  if (!imported) {
    return EXIT_FAILURE;
  }

  flux::parser::ImportDeclaration plain_import;
  plain_import.name = imported_module.name;

  flux::parser::ImportDeclaration glob_import;
  glob_import.name = imported_module.name;
  glob_import.selector = flux::parser::ImportAll{};

  flux::parser::ImportDeclaration alias_import;
  alias_import.name = imported_module.name;
  alias_import.selector = flux::parser::ImportAlias{
      .alias = {.kind = flux::TokenKind::Identifier, .lexeme = "lib"}};

  flux::parser::ImportDeclaration items_import;
  items_import.name = imported_module.name;
  items_import.selector = flux::parser::ImportItems{
      .items = {
          {.name = {.kind = flux::TokenKind::Identifier, .lexeme = "Item"},
           .alias = flux::Token{.kind = flux::TokenKind::Identifier,
                                .lexeme = "RenamedItem"}}}};

  flux::parser::ModuleDeclaration current_module;
  current_module.name.parts = {
      {.kind = flux::TokenKind::Identifier, .lexeme = "app"}};
  program.module = &current_module;
  program.imports = {&plain_import, &glob_import, &alias_import, &items_import};
  resolver.resolve(program);

  const auto current = context.modules.find(resolver.names().intern("app"));
  if (!current || context.modules.get(*current).imports.size() != 1 ||
      context.modules.get(*current).imports.front() != *imported ||
      resolver.names().get(resolver.names().intern("lib")) != "lib" ||
      resolver.names().get(resolver.names().intern("Item")) != "Item" ||
      resolver.names().get(resolver.names().intern("RenamedItem")) !=
          "RenamedItem") {
    return EXIT_FAILURE;
  }

  const auto root_scope = context.modules.get(*current).root_scope;
  if (!root_scope) {
    return EXIT_FAILURE;
  }
  const Scope &module_scope = context.scopes.at(root_scope->value);
  const auto plain_binding =
      module_scope.bindings.find(resolver.names().intern("library"));
  const auto alias_binding =
      module_scope.bindings.find(resolver.names().intern("lib"));
  const auto item_binding =
      module_scope.bindings.find(resolver.names().intern("RenamedItem"));
  if (plain_binding == module_scope.bindings.end() ||
      alias_binding == module_scope.bindings.end() ||
      item_binding == module_scope.bindings.end() ||
      plain_binding->second.origin != BindingOrigin::ModuleImport ||
      std::get<ModuleId>(plain_binding->second.target) != *imported ||
      alias_binding->second.origin != BindingOrigin::ImportAlias ||
      std::get<ModuleId>(alias_binding->second.target) != *imported ||
      item_binding->second.origin != BindingOrigin::SelectedImport ||
      std::get<ImportedSymbol>(item_binding->second.target) !=
          ImportedSymbol{.module = *imported,
                         .name = resolver.names().intern("Item")}) {
    return EXIT_FAILURE;
  }

  SemanticContext declaration_context;
  NameResolver declaration_resolver(declaration_context, arena);
  const auto identifier = [](std::string_view name) {
    return flux::Token{.kind = flux::TokenKind::Identifier,
                       .lexeme = std::string(name)};
  };

  flux::parser::ModuleDeclaration declaration_module;
  declaration_module.name.parts = {identifier("declarations")};
  flux::parser::FunctionDeclaration function;
  function.signature.name = identifier("function");
  flux::parser::ExternalFunctionDeclaration external_function;
  external_function.is_public = true;
  external_function.signature.name = identifier("external_function");
  flux::parser::TypeAliasDeclaration type_alias;
  type_alias.name = identifier("TypeAlias");
  flux::parser::RecordTypeDeclaration record;
  record.is_public = true;
  record.name = identifier("Record");
  flux::parser::SumTypeDeclaration sum;
  sum.name = identifier("Sum");
  sum.generic_parameters = {{.name = identifier("T")}};
  flux::parser::TraitDeclaration trait;
  trait.is_public = true;
  trait.name = identifier("Trait");
  flux::parser::ImplDeclaration implementation;
  flux::parser::ConstantDeclaration constant;
  constant.name = identifier("constant");
  flux::parser::EffectDeclaration effect;
  effect.name = identifier("Effect");
  flux::parser::DomainDeclaration domain;
  domain.is_public = true;
  domain.name = identifier("Domain");

  Program declaration_program;
  declaration_program.module = &declaration_module;
  declaration_program.declarations = {
      &function, &external_function, &type_alias, &record, &sum,
      &trait,    &implementation,    &constant,   &effect, &domain};
  declaration_resolver.resolve(declaration_program);

  const auto declarations = declaration_context.modules.find(
      declaration_resolver.names().intern("declarations"));
  if (!declarations) {
    return EXIT_FAILURE;
  }
  const auto declaration_scope_id =
      declaration_context.modules.get(*declarations).root_scope;
  const auto &declaration_symbols =
      declaration_context.modules.get(*declarations).symbols;
  if (!declaration_scope_id || declaration_symbols.size() != 9) {
    return EXIT_FAILURE;
  }
  const Scope &declaration_scope =
      declaration_context.scopes.at(declaration_scope_id->value);
  const auto has_declaration = [&](std::string_view name, SymbolKind kind,
                                   bool is_public) {
    const auto binding = declaration_scope.bindings.find(
        declaration_resolver.names().intern(name));
    if (binding == declaration_scope.bindings.end() ||
        binding->second.origin != BindingOrigin::Declaration ||
        !std::holds_alternative<flux::semantic_analysis::SymbolId>(
            binding->second.target)) {
      return false;
    }
    const Symbol &declared = declaration_symbols.get(
        std::get<flux::semantic_analysis::SymbolId>(binding->second.target));
    return declared.owner_scope == *declaration_scope_id &&
           declared.kind == kind && declared.is_public == is_public &&
           declaration_resolver.names().get(declared.name) == name;
  };
  if (!has_declaration("function", SymbolKind::Function, false) ||
      !has_declaration("external_function", SymbolKind::ExternalFunction,
                       true) ||
      !has_declaration("TypeAlias", SymbolKind::Type, false) ||
      !has_declaration("Record", SymbolKind::Type, true) ||
      !has_declaration("Sum", SymbolKind::Type, false) ||
      !has_declaration("Trait", SymbolKind::Trait, true) ||
      !has_declaration("constant", SymbolKind::Constant, false) ||
      !has_declaration("Effect", SymbolKind::Effect, false) ||
      !has_declaration("Domain", SymbolKind::Domain, true) ||
      declaration_scope.bindings.contains(
          declaration_resolver.names().intern("T"))) {
    return EXIT_FAILURE;
  }

  SemanticContext resolution_context;
  NameResolver resolution_resolver(resolution_context, arena);
  flux::parser::ModuleDeclaration names_module;
  names_module.name.parts = {identifier("names")};
  flux::parser::TypeAliasDeclaration imported_type;
  imported_type.name = identifier("ImportedType");
  Program names_program;
  names_program.module = &names_module;
  names_program.declarations = {&imported_type};
  resolution_resolver.resolve(names_program);

  flux::parser::ModuleDeclaration lookup_module;
  lookup_module.name.parts = {identifier("lookup")};
  flux::parser::ImportDeclaration names_import;
  names_import.name = names_module.name;
  flux::parser::TypeAliasDeclaration local_type;
  local_type.name = identifier("LocalType");

  flux::parser::TypeExpression local_reference;
  local_reference.value =
      flux::parser::TypeName{.name = {.parts = {identifier("LocalType")}}};
  flux::parser::TypeAliasDeclaration local_alias;
  local_alias.name = identifier("LocalAlias");
  local_alias.type = &local_reference;

  flux::parser::TypeExpression imported_reference;
  imported_reference.value = flux::parser::TypeName{
      .name = {.parts = {identifier("names"), identifier("ImportedType")}}};
  flux::parser::TypeAliasDeclaration imported_alias;
  imported_alias.name = identifier("ImportedAlias");
  imported_alias.type = &imported_reference;

  flux::Token missing_name = identifier("MissingType");
  missing_name.location = {.offset = 40, .line = 3, .column = 7};
  flux::parser::TypeExpression missing_reference;
  missing_reference.value =
      flux::parser::TypeName{.name = {.parts = {missing_name}}};
  flux::parser::TypeAliasDeclaration missing_alias;
  missing_alias.name = identifier("MissingAlias");
  missing_alias.type = &missing_reference;

  Program lookup_program;
  lookup_program.module = &lookup_module;
  lookup_program.imports = {&names_import};
  lookup_program.declarations = {&local_type, &local_alias, &imported_alias,
                                 &missing_alias};
  resolution_resolver.resolve(lookup_program);
  if (resolution_context.errors.size() != 1 ||
      resolution_context.errors.front().code !=
          SemanticErrorCode::UnresolvedName ||
      resolution_context.errors.front().message !=
          "unresolved name 'MissingType'" ||
      resolution_context.errors.front().span.start.offset != 40) {
    return EXIT_FAILURE;
  }

  SemanticContext circular_context;
  NameResolver circular_resolver(circular_context, arena);

  flux::parser::ModuleDeclaration module_a;
  module_a.name.parts = {{.kind = flux::TokenKind::Identifier, .lexeme = "a"}};
  flux::parser::ModuleDeclaration module_b;
  module_b.name.parts = {{.kind = flux::TokenKind::Identifier, .lexeme = "b"}};

  flux::parser::ImportDeclaration import_b;
  import_b.name = module_b.name;
  Program program_a;
  program_a.module = &module_a;
  program_a.imports = {&import_b};
  circular_resolver.resolve(program_a);

  flux::parser::ImportDeclaration import_a;
  import_a.name = module_a.name;
  Program program_b;
  program_b.module = &module_b;
  program_b.imports = {&import_a};
  circular_resolver.resolve(program_b);

  const auto a =
      circular_context.modules.find(circular_resolver.names().intern("a"));
  const auto b =
      circular_context.modules.find(circular_resolver.names().intern("b"));
  if (!a || !b || circular_context.modules.size() != 2 ||
      circular_context.modules.get(*a).imports.size() != 1 ||
      circular_context.modules.get(*a).imports.front() != *b ||
      circular_context.modules.get(*b).imports.size() != 1 ||
      circular_context.modules.get(*b).imports.front() != *a) {
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

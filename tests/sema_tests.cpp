#include "flux/sema/name_resolver.hpp"

#include <cstdlib>

using flux::parser::BumpAllocator;
using flux::parser::Program;
using flux::semantic_analysis::NameResolver;
using flux::semantic_analysis::BindingOrigin;
using flux::semantic_analysis::ImportedSymbol;
using flux::semantic_analysis::ModuleId;
using flux::semantic_analysis::Scope;
using flux::semantic_analysis::SemanticContext;
using flux::semantic_analysis::Symbol;
using flux::semantic_analysis::SymbolKind;
using flux::semantic_analysis::SymbolTable;

int main() {
  SymbolTable symbols;
  const auto symbol = symbols.add(Symbol{.owner_scope = 1,
                                         .kind = SymbolKind::Local,
                                         .name = 0,
                                         .declaration_span = {}});
  if (symbols.get(symbol).owner_scope != 1) {
    return EXIT_FAILURE;
  }

  SemanticContext context;
  const auto module = context.modules.register_module(7);
  if (module.value != 0 || context.modules.size() != 1 ||
      context.modules.get(module).interned_name != 7 ||
      !context.modules.get(module).imports.empty() ||
      context.modules.get(module).symbols.size() != 0) {
    return EXIT_FAILURE;
  }

  context.scopes.push_back(Scope{.parent = std::nullopt});
  context.scopes.push_back(Scope{.parent = 0});
  if (context.scopes[1].parent != 0) {
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

  flux::parser::ModuleDeclaration imported_module;
  imported_module.name.parts = {
      {.kind = flux::TokenKind::Identifier, .lexeme = "library"}};
  Program imported_program;
  imported_program.module = &imported_module;
  resolver.resolve(imported_program);
  const auto imported = context.modules.find(resolver.names().intern("library"));
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
      .items = {{.name = {.kind = flux::TokenKind::Identifier,
                          .lexeme = "Item"},
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
  const Scope &module_scope = context.scopes.at(*root_scope);
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

  SemanticContext circular_context;
  NameResolver circular_resolver(circular_context, arena);

  flux::parser::ModuleDeclaration module_a;
  module_a.name.parts = {
      {.kind = flux::TokenKind::Identifier, .lexeme = "a"}};
  flux::parser::ModuleDeclaration module_b;
  module_b.name.parts = {
      {.kind = flux::TokenKind::Identifier, .lexeme = "b"}};

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

  const auto a = circular_context.modules.find(circular_resolver.names().intern("a"));
  const auto b = circular_context.modules.find(circular_resolver.names().intern("b"));
  if (!a || !b || circular_context.modules.size() != 2 ||
      circular_context.modules.get(*a).imports.size() != 1 ||
      circular_context.modules.get(*a).imports.front() != *b ||
      circular_context.modules.get(*b).imports.size() != 1 ||
      circular_context.modules.get(*b).imports.front() != *a) {
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

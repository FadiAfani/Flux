#pragma once

#include "flux/lexer/token.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <variant>
#include <vector>

namespace flux::semantic_analysis {

using SymbolTableId = std::uint32_t;

struct ScopeId {
  std::uint32_t value = 0;
  friend bool operator==(ScopeId, ScopeId) = default;
};

struct NameId {
  std::uint32_t value = 0;
  friend bool operator==(NameId, NameId) = default;
};

struct NameIdHash {
  std::size_t operator()(NameId id) const noexcept { return id.value; }
};

struct SymbolId {
  std::uint32_t value = 0;
  friend bool operator==(SymbolId, SymbolId) = default;
};

struct ModuleId {
  std::uint32_t value = 0;
  friend bool operator==(ModuleId, ModuleId) = default;
};

struct ImportedSymbol {
  ModuleId module;
  NameId name;
  friend bool operator==(ImportedSymbol, ImportedSymbol) = default;
};

enum class SymbolKind {
  Constant,
  Function,
  ExternalFunction,
  Type,
  Trait,
  GenericParameter,
  Effect,
  Capability,
  Domain,
  Parameter,
  Local,
  Variant,
  Field,
};

struct Symbol {
  ScopeId owner_scope;
  SymbolKind kind;
  NameId name;
  flux::SourceSpan declaration_span;
  bool is_public = false;
};

struct SymbolTable {
  SymbolId add(Symbol symbol);
  const Symbol &get(SymbolId symbol) const;
  Symbol &get_mut(SymbolId symbol);
  std::size_t size() const noexcept;

private:
  std::optional<SymbolTableId> parent_;
  std::vector<Symbol> symbols_;
};

using BindingTarget = std::variant<SymbolId, ModuleId, ImportedSymbol>;

enum class BindingOrigin {
  Declaration,
  ImportAlias,
  ModuleImport,
  SelectedImport,
};

struct Binding {
  BindingTarget target;
  BindingOrigin origin;
  SourceSpan defined_at;
};

struct Scope {
  std::optional<ScopeId> parent;
  std::unordered_map<NameId, Binding, NameIdHash> bindings;
};

class ScopeRegistry {
public:
  void register_scope();
  void register_scope(Scope scope);
  Scope &get_mut(ScopeId id);
  const Scope &get(ScopeId id);

private:
  std::vector<Scope> scopes_;
};

} // namespace flux::semantic_analysis

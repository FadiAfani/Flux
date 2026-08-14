#pragma once

#include "flux/sema/symbol.hpp"

#include <optional>
#include <vector>

namespace flux::semantic_analysis {

struct Module {
  NameId interned_name;
  std::optional<ScopeId> root_scope;
  SymbolTable symbols;
  std::vector<ModuleId> imports;
};

class ModuleRegistry {
public:
  ModuleId register_module(NameId name);
  std::optional<ModuleId> find(NameId name) const;
  const Module &get(ModuleId id) const;
  Module &get_mut(ModuleId id);
  std::size_t size() const noexcept;

private:
  std::vector<Module> modules_;
};

} // namespace flux::semantic_analysis

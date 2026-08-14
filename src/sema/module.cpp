#include "flux/sema/module.hpp"

#include <limits>
#include <stdexcept>

namespace flux::semantic_analysis {

ModuleId ModuleRegistry::register_module(NameId name) {
  if (const std::optional<ModuleId> existing = find(name)) {
    return *existing;
  }

  if (modules_.size() > std::numeric_limits<std::uint32_t>::max()) {
    throw std::length_error("module registry exhausted its ID space");
  }

  const ModuleId id{.value = static_cast<std::uint32_t>(modules_.size())};
  modules_.push_back({.interned_name = name});
  return id;
}

std::optional<ModuleId> ModuleRegistry::find(NameId name) const {
  for (std::size_t index = 0; index < modules_.size(); ++index) {
    if (modules_[index].interned_name == name) {
      return ModuleId{.value = static_cast<std::uint32_t>(index)};
    }
  }
  return std::nullopt;
}

const Module &ModuleRegistry::get(ModuleId id) const {
  return modules_.at(id.value);
}

Module &ModuleRegistry::get_mut(ModuleId id) {
  return modules_.at(id.value);
}

std::size_t ModuleRegistry::size() const noexcept { return modules_.size(); }

} // namespace flux::semantic_analysis

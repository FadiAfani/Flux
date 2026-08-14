#pragma once

#include "flux/sema/module.hpp"
#include "flux/sema/symbol.hpp"

namespace flux::semantic_analysis {

struct SemanticContext {
  ModuleRegistry modules;
  std::vector<Scope> scopes;
};

} // namespace flux::semantic_analysis

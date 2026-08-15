#pragma once

#include "flux/sema/error.hpp"
#include "flux/sema/module.hpp"
#include "flux/sema/symbol.hpp"

#include <stack>
#include <string>
#include <utility>
#include <vector>

namespace flux::semantic_analysis {

struct SemanticContext {
  ModuleRegistry modules;
  std::vector<Scope> scopes;
  std::vector<SemanticError> errors;

  void report_error(SemanticErrorCode code, SourceSpan span,
                    std::string message) {
    errors.push_back(
        {.code = code, .message = std::move(message), .span = span});
  }
};

} // namespace flux::semantic_analysis

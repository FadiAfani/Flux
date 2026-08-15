#pragma once

#include "flux/lexer/token.hpp"

#include <string>

namespace flux::semantic_analysis {

enum class SemanticErrorCode {
  MissingModuleDeclaration,
  UnresolvedName,
};

struct SemanticError {
  SemanticErrorCode code;
  std::string message;
  SourceSpan span;
};

} // namespace flux::semantic_analysis

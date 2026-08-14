#include "flux/sema/symbol.hpp"

#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace flux::semantic_analysis {
namespace {

template <typename Id> Id next_id(std::size_t size, const char *table_name) {
  if (size > std::numeric_limits<Id>::max()) {
    throw std::length_error(std::string(table_name) +
                            " exhausted its ID space");
  }
  return static_cast<Id>(size);
}

} // namespace

SymbolId SymbolTable::add(Symbol symbol) {
  const auto value = next_id<std::uint32_t>(symbols_.size(), "symbol table");
  symbols_.push_back(std::move(symbol));
  return {.value = value};
}

const Symbol &SymbolTable::get(SymbolId symbol) const {
  return symbols_.at(symbol.value);
}

Symbol &SymbolTable::get_mut(SymbolId symbol) {
  return symbols_.at(symbol.value);
}

std::size_t SymbolTable::size() const noexcept { return symbols_.size(); }

} // namespace flux::semantic_analysis

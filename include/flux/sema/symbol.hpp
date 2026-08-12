#include "flux/lexer/token.hpp"
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>
namespace  SemanticAnalysis {

    using ScopeId = uint32_t;
    using NameId = uint32_t;
    using SymbolId = uint32_t;

    enum class SymbolKind {
        Module,
        Import,
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
        ScopeId scope;
        SymbolKind kind;
        NameId name;
        flux::SourceSpan decl_span;
        bool is_public = false;
    };

    struct Scope {
        std::optional<ScopeId> parent;
        std::unordered_map<NameId, SymbolId> symbol_table;
    };

    class ScopeTable {
        public:
            ScopeId create(std::optional<ScopeId> parent);
            std::optional<SymbolId> lookup(ScopeId scope, NameId name);
            bool insert(ScopeId scope, NameId name, SymbolId symbol);

        private:
            std::vector<Scope> scopes_;
    };

}

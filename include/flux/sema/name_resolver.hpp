#include "flux/parser/allocator.hpp"
#include <cstdint>
#include <string_view>
#include <unordered_map>
namespace SemanticAnalysis {
    using NameId = std::uint32_t;

    class NameResolver {
        public:
            explicit NameResolver(flux::parser::BumpAllocator& arena) : arena_(arena) {}

        private:
            flux::parser::BumpAllocator& arena_;
    };

    class NameInterner {

        public:
            NameId intern(std::string_view name);
            std::string_view get(NameId id) const;

        private:
            std::unordered_map<std::string, NameId> ids_;

    };
}

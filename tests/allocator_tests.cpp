#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string_view>

#include "flux/parser/allocator.hpp"

using flux::parser::BumpAllocator;

namespace {

void fail(std::string_view message) {
    std::cerr << message << '\n';
    std::exit(EXIT_FAILURE);
}

struct Constructed {
    int first;
    int second;

    Constructed(int lhs, int rhs) : first(lhs), second(rhs) {}
};

struct alignas(64) CacheAligned {
    std::uint8_t value;
};

struct BlockSized {
    std::byte bytes[BLOCK_SIZE];
};

struct DestructionTracked {
    bool* destroyed;

    ~DestructionTracked() {
        *destroyed = true;
    }
};

void constructs_object_with_forwarded_arguments() {
    BumpAllocator allocator;

    Constructed* object = allocator.create<Constructed>(17, 25);

    if (object == nullptr) {
        fail("allocator returned a null object pointer");
    }

    if (object->first != 17 || object->second != 25) {
        fail("allocator did not construct the object with the expected values");
    }
}

void returns_distinct_storage_for_multiple_objects() {
    BumpAllocator allocator;

    int* first = allocator.create<int>(11);
    int* second = allocator.create<int>(29);

    if (first == nullptr || second == nullptr) {
        fail("allocator returned a null pointer for an int allocation");
    }

    if (first == second) {
        fail("allocator returned the same address for two live objects");
    }

    if (*first != 11 || *second != 29) {
        fail("allocator did not preserve values across multiple allocations");
    }
}

void respects_requested_type_alignment() {
    BumpAllocator allocator;

    CacheAligned* object = allocator.create<CacheAligned>();
    const auto address = reinterpret_cast<std::uintptr_t>(object);

    if (address % alignof(CacheAligned) != 0) {
        fail("allocator returned storage that does not satisfy type alignment");
    }
}

void handles_many_small_allocations() {
    BumpAllocator allocator;

    constexpr int allocation_count = 512;
    int* values[allocation_count] = {};

    for (int i = 0; i < allocation_count; ++i) {
        values[i] = allocator.create<int>(i);
    }

    for (int i = 0; i < allocation_count; ++i) {
        if (values[i] == nullptr || *values[i] != i) {
            fail("allocator failed to retain a value from repeated allocations");
        }
    }
}

void created_blocks_respect_size_constant() {
    BumpAllocator allocator;

    BlockSized* full_block = allocator.create<BlockSized>();
    int* next = allocator.create<int>(123);

    full_block->bytes[0] = std::byte{0x11};
    full_block->bytes[BLOCK_SIZE - 1] = std::byte{0x22};

    if (full_block->bytes[0] != std::byte{0x11} ||
        full_block->bytes[BLOCK_SIZE - 1] != std::byte{0x22}) {
        fail("allocator did not provide writable storage across a full block");
    }

    const auto block_start = reinterpret_cast<std::uintptr_t>(full_block);
    const auto block_end = block_start + sizeof(BlockSized);
    const auto next_address = reinterpret_cast<std::uintptr_t>(next);

    if (next_address >= block_start && next_address < block_end) {
        fail("allocator placed an object inside storage already used by a full block");
    }

    if (*next != 123) {
        fail("allocator did not preserve value allocated after a full block");
    }
}

void destroys_non_trivial_objects() {
    bool destroyed = false;

    {
        BumpAllocator allocator;
        allocator.create<DestructionTracked>(&destroyed);
    }

    if (!destroyed) {
        fail("allocator did not destroy a non-trivial object");
    }
}

} // namespace

int main() {
    constructs_object_with_forwarded_arguments();
    returns_distinct_storage_for_multiple_objects();
    respects_requested_type_alignment();
    handles_many_small_allocations();
    created_blocks_respect_size_constant();
    destroys_non_trivial_objects();

    return EXIT_SUCCESS;
}

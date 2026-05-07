#include <cstddef>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

#define BLOCK_SIZE 1024 * 64
#define SCALING_FACTOR 1.5

namespace flux::parser {

    /**
     * Used to allocate AST nodes in a buffer instead of relying on heap allocations
     * Allows AST do be deallocated all at once
     */

    struct Block {
        size_t len = 0;
        size_t cap;
        std::unique_ptr<std::byte[]> bytes;
    };
    class BumpAllocator {

        private:
        std::vector<Block> blocks_;
        std::byte* cur_;

        void* allocate(size_t size, size_t alignment) {
            auto& last = blocks_.back();
            std::byte* end = last.bytes.get() + last.cap - 1;
            std::byte* target = cur_ + static_cast<size_t>(cur_ - last.bytes.get()) % alignment;

            if (target + size > end) {
                size_t gap = target > end ? static_cast<size_t>(target - end) : 0;
                new_block(gap + SCALING_FACTOR * size);
            }
            cur_ = target + size;

            return target;

        }

        void new_block(size_t size) {
            auto bytes = std::make_unique<std::byte[]>(size);
            Block b = {.len = 0, .cap = size, .bytes = std::move(bytes)};
            cur_ = b.bytes.get();
            blocks_.push_back(std::move(b));
        }

        public:
        BumpAllocator() {
            new_block(BLOCK_SIZE);
        }

        template<typename T, typename ...Args>
        T* create(Args&& ...args) {
            size_t size = sizeof(T);
            size_t alignment = alignof(T);

            void* obj = allocate(size, alignment);

            return new (obj) T(std::forward<Args>(args)...);
        }
    };
}

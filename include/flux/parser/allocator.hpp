#pragma once

#include <algorithm>
#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

#define BLOCK_SIZE 1024 * 64

namespace flux::parser {

/**
 * Used to allocate AST nodes in a buffer instead of relying on heap
 * allocations. All objects and backing storage are released with the allocator.
 */

class BumpAllocator {
private:
  struct ArenaBlock {
    size_t len = 0;
    size_t cap;
    std::unique_ptr<std::byte[]> bytes;
  };

  struct Destructor {
    void *object;
    void (*destroy)(void *) noexcept;
  };

  std::vector<ArenaBlock> blocks_;
  std::vector<Destructor> destructors_;
  std::byte *cur_ = nullptr;

  void *allocate(size_t size, size_t alignment) {
    auto *target = static_cast<void *>(cur_);
    auto &last = blocks_.back();
    size_t space = last.cap - last.len;

    if (std::align(alignment, size, target, space) == nullptr) {
      const auto required = size + alignment - 1;
      new_block(std::max(static_cast<size_t>(BLOCK_SIZE), required));

      auto &new_last = blocks_.back();
      target = static_cast<void *>(cur_);
      space = new_last.cap;
      std::align(alignment, size, target, space);
    }

    auto &active = blocks_.back();
    cur_ = static_cast<std::byte *>(target) + size;
    active.len = static_cast<size_t>(cur_ - active.bytes.get());
    return target;
  }

  void new_block(size_t size) {
    auto bytes = std::make_unique<std::byte[]>(size);
    ArenaBlock block = {.len = 0, .cap = size, .bytes = std::move(bytes)};
    cur_ = block.bytes.get();
    blocks_.push_back(std::move(block));
  }

public:
  BumpAllocator() { new_block(BLOCK_SIZE); }

  BumpAllocator(const BumpAllocator &) = delete;
  BumpAllocator &operator=(const BumpAllocator &) = delete;
  BumpAllocator(BumpAllocator &&) = delete;
  BumpAllocator &operator=(BumpAllocator &&) = delete;

  ~BumpAllocator() {
    for (auto it = destructors_.rbegin(); it != destructors_.rend(); ++it) {
      it->destroy(it->object);
    }
  }

  template <typename T, typename... Args> T *create(Args &&...args) {
    void *storage = allocate(sizeof(T), alignof(T));
    T *object = new (storage) T(std::forward<Args>(args)...);

    if constexpr (!std::is_trivially_destructible_v<T>) {
      try {
        destructors_.push_back(
            {.object = object, .destroy = [](void *value) noexcept {
               static_cast<T *>(value)->~T();
             }});
      } catch (...) {
        object->~T();
        throw;
      }
    }

    return object;
  }
};

} // namespace flux::parser

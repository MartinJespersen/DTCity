#pragma once

#include "base_arena.hpp"

#include <mimalloc.h>
#include "base_core.hpp"

///////////////////////////////////////////////////////////////////////
// Heap-backed contiguous array. Growth invalidates pointers and iterators.
template <typename T>
struct DynamicArray
{
    T* data = {};
    U64 size = {};
    U64 capacity = {};

    static_assert(std::is_trivially_copyable_v<T> && std::is_trivially_destructible_v<T>,
                  "DynamicArray requires trivially copyable elements");

    DynamicArray() = default;
    explicit DynamicArray(U64 initial_item_capacity) noexcept;
    ~DynamicArray();
    DynamicArray(const DynamicArray&) = delete;
    DynamicArray&
    operator=(const DynamicArray&) = delete;
    DynamicArray(DynamicArray&& other) noexcept;
    DynamicArray&
    operator=(DynamicArray&& other) noexcept;

    void
    reserve(U64 item_capacity) noexcept;
    T*
    push(const T& item) noexcept;
    void
    push(const T* items, U64 item_count) noexcept;
    void
    clear() noexcept;
    void
    release() noexcept;
    T&
    operator[](U64 index) noexcept;
    const T&
    operator[](U64 index) const noexcept;
    T*
    begin() noexcept;
    const T*
    begin() const noexcept;
    T*
    end() noexcept;
    const T*
    end() const noexcept;
};

///////////////////////////////////////////////////////////////////////
// DynamicArenaArray
template <typename T>
struct ArenaArray
{
  private:
    Arena* _arena;
    T* _arr;
    U64 _capacity;

  public:
    U64 size;

    ArenaArray(U64 initial_item_capacity, U64 reserve_byte_capacity = KB(64), ArenaFlags arena_flags = 0) noexcept;
    ~ArenaArray();

    static void
    release(ArenaArray<T>* arr) noexcept;

    T&
    operator[](U64 index) noexcept
    {
        AssertAlways(index < size);
        return _arr[index];
    }

    T*
    begin() noexcept
    {
        return _arr;
    }

    T*
    end() noexcept
    {
        return _arr + size;
    }

    T*
    push(T& item) noexcept;

    void
    push(T* arr, U64 size) noexcept;

    void
    clear() noexcept;
};

// Resource Pool

struct PoolHandle
{
    U32 idx;
    U32 gen_id;

    bool
    operator==(PoolHandle& other)
    {
        return idx == other.idx && gen_id == other.gen_id;
    }
};

template <typename T>
struct ArrayItemHeader
{
    U32 next;
    U32 gen_id;
    bool in_use;
    bool constructed; // emplace owns this object; raw slots retain caller-managed lifetime.
    T data;
};

template <typename T>
struct ResourcePool;

template <typename T>
struct PoolIterator
{
    ResourcePool<T>* pool;
    U32 idx;

    T&
    operator*();

    T*
    operator->();

    PoolIterator<T>&
    operator++();

    bool
    operator!=(PoolIterator<T> other);

    bool
    operator==(PoolIterator<T> other);

    void
    skip_unused();
};

template <typename T>
struct ResourcePool
{
    ArrayItemHeader<T>* items;
    U32 capacity;
    U32 free_list;
    U32 item_in_use_count;

    ResourcePool(Arena* arena, U32 capacity) noexcept;
    ~ResourcePool() noexcept;
    ResourcePool(const ResourcePool&) = delete;
    ResourcePool& operator=(const ResourcePool&) = delete;

    template <typename... Args>
    PoolHandle
    emplace(Args&&... args) noexcept;
    static ResourcePool<T>*
    create(Arena* arena, U32 capacity);

    void
    init(Arena* arena, U32 capacity);
    PoolHandle
    item_new(T** out_item);

    bool
    item_from_handle(PoolHandle item_handle, T** out_value);

    PoolHandle
    handle_get();

    void
    invalidate_all();

    bool
    is_handle_valid(PoolHandle item_handle);

    void
    item_free(PoolHandle item_handle);

    PoolIterator<T>
    begin();

    PoolIterator<T>
    end();

  private:
    void
    reset_freelist();
};

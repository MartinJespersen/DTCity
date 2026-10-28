#pragma once

#include "base_container.hpp"
#include "os_core/os_core_inc.hpp"
#include "debug_forward_ref.hpp"

// Template implementations
///////////////////////////////////////////////////////////////////////////////////////
// Dynamic Array

template <typename T>
DynamicArray<T>::DynamicArray(U64 initial_item_capacity) noexcept
{
    reserve(initial_item_capacity);
}

template <typename T>
DynamicArray<T>::~DynamicArray()
{
    release();
}

template <typename T>
DynamicArray<T>::DynamicArray(DynamicArray&& other) noexcept
{
    *this = std::move(other);
}

template <typename T>
DynamicArray<T>&
DynamicArray<T>::operator=(DynamicArray&& other) noexcept
{
    if (this != &other)
    {
        release();
        data = other.data;
        size = other.size;
        capacity = other.capacity;
        other.data = 0;
        other.size = 0;
        other.capacity = 0;
    }
    return *this;
}

template <typename T>
void
DynamicArray<T>::reserve(U64 item_capacity) noexcept
{
    if (item_capacity <= capacity)
    {
        return;
    }
    AssertAlways(item_capacity <= SIZE_MAX / sizeof(T));
    U64 byte_size = item_capacity * sizeof(T);
    // The aligned variants also support SIMD and other over-aligned element types.
    T* allocation = (T*)mi_realloc_aligned(data, byte_size, alignof(T));
    AssertAlways(allocation);
    data = allocation;
    capacity = item_capacity;
}

template <typename T>
T*
DynamicArray<T>::push(const T& item) noexcept
{
    // Copy first: item may refer to an element invalidated by growth.
    T copy = item;
    push(&copy, 1);
    return data + size - 1;
}

template <typename T>
void
DynamicArray<T>::push(const T* items, U64 item_count) noexcept
{
    if (item_count == 0)
    {
        return;
    }
    U64 max_capacity = SIZE_MAX / sizeof(T);
    AssertAlways(item_count <= max_capacity - size);
    U64 required_capacity = size + item_count;

    // Preserve a source range within this array across reallocation (self-append).
    U64 source_address = (U64)items;
    U64 data_address = (U64)data;
    B32 source_is_internal = data && source_address >= data_address && source_address < data_address + size * sizeof(T);
    U64 source_offset = source_is_internal ? (source_address - data_address) / sizeof(T) : 0;
    Assert(!source_is_internal || item_count <= size - source_offset);
    if (required_capacity > capacity)
    {
        U64 grown_capacity = capacity > max_capacity / 2 ? max_capacity : capacity * 2;
        grown_capacity = Max(grown_capacity, Min((U64)8, max_capacity));
        reserve(Max(required_capacity, grown_capacity));
    }
    if (source_is_internal)
    {
        items = data + source_offset;
    }
    MemoryCopy(data + size, items, item_count * sizeof(T));
    size = required_capacity;
}

template <typename T>
void
DynamicArray<T>::clear() noexcept
{
    size = 0;
}

template <typename T>
void
DynamicArray<T>::release() noexcept
{
    mi_free(data);
    data = 0;
    size = 0;
    capacity = 0;
}

template <typename T>
T&
DynamicArray<T>::operator[](U64 index) noexcept
{
    Assert(index < size);
    return data[index];
}

template <typename T>
const T&
DynamicArray<T>::operator[](U64 index) const noexcept
{
    Assert(index < size);
    return data[index];
}

template <typename T>
T*
DynamicArray<T>::begin() noexcept
{
    return data;
}

template <typename T>
const T*
DynamicArray<T>::begin() const noexcept
{
    return data;
}

template <typename T>
T*
DynamicArray<T>::end() noexcept
{
    return size ? data + size : data;
}

template <typename T>
const T*
DynamicArray<T>::end() const noexcept
{
    return size ? data + size : data;
}

///////////////////////////////////////////////////////////////////////////////////////
// Arena Array

template <typename T>
ArenaArray<T>::ArenaArray(U64 initial_item_capacity, U64 reserve_byte_capacity, ArenaFlags arena_flags) noexcept
{
    U64 element_byte_size = sizeof(T);
    // ARENA_HEADER_SIZE have
    OS_SystemInfo* system_info = OS_GetSystemInfo();
    U64 page_size = system_info->page_size;
    if (arena_flags & ArenaFlag_LargePages)
    {
        page_size = system_info->large_page_size;
    }
    U64 initial_commit_size_page_align =
        align_pow2(element_byte_size * initial_item_capacity + ARENA_HEADER_SIZE, page_size);
    U64 reserve_page_align = align_pow2(reserve_byte_capacity, page_size);
    ArenaParams arena_params = {};
    arena_params.reserve_size = Max(reserve_page_align, page_size);
    arena_params.commit_size = Max(initial_commit_size_page_align, page_size);
    arena_params.flags = arena_flags;
    arena_params.flags |= ArenaFlag_NoChain;
    arena_params.flags |= ArenaFlag_RetainCommitted;
    Arena* arena = arena_alloc(&arena_params);
    Debug_SetName(arena, "fixed dynamic array arena");
    this->_arena = arena;
    this->_arr = (T*)((U8*)arena + arena_pos(arena));
    this->_capacity = (reserve_page_align - ARENA_HEADER_SIZE) / element_byte_size;
    this->size = 0;
}

template <typename T>
ArenaArray<T>::~ArenaArray()
{
    arena_release(_arena);
}

template <typename T>
void
ArenaArray<T>::release(ArenaArray<T>* arr) noexcept
{
    arr->~ArenaArray();
}

template <typename T>
T*
ArenaArray<T>::push(T& item) noexcept
{
    AssertAlways(size < _capacity);
    T* i = PushStruct(_arena, T);
    _arr[size++] = item;
    return i;
}

template <typename T>
void
ArenaArray<T>::push(T* arr, U64 size) noexcept
{
    AssertAlways(size + this->size <= _capacity);
    T* dest = PushArray(_arena, T, size);
    MemoryCopy(dest, arr, size * sizeof(T));
    this->size += size;
}

template <typename T>
void
ArenaArray<T>::clear() noexcept
{
    this->size = 0;
    arena_clear(this->_arena);
}

//////////////////////////////////////////////////////////////
// Array Resource Pool
template <typename T>
ResourcePool<T>::ResourcePool(Arena* arena, U32 capacity) noexcept
{
    this->init(arena, capacity);
}

template <typename T>
ResourcePool<T>::~ResourcePool() noexcept
{
    this->invalidate_all();
}

template <typename T>
template <typename... Args>
PoolHandle
ResourcePool<T>::emplace(Args&&... args) noexcept
{
    PoolHandle handle = this->handle_get();
    ArrayItemHeader<T>* item = &this->items[handle.idx];
    new (&item->data) T(std::forward<Args>(args)...);
    item->constructed = true;
    return handle;
}

template <typename T>
void
ResourcePool<T>::init(Arena* arena, U32 capacity)
{
    this->capacity = Max(capacity, 1);
    this->items = PushArray(arena, ArrayItemHeader<T>, this->capacity + 1);
    this->item_in_use_count = 0;
    this->reset_freelist();
}

template <typename T>
ResourcePool<T>*
ResourcePool<T>::create(Arena* arena, U32 capacity)
{
    ResourcePool* pool = PushStruct(arena, ResourcePool<T>);
    new (pool) ResourcePool<T>(arena, capacity);
    return pool;
}

template <typename T>
bool
ResourcePool<T>::is_handle_valid(PoolHandle item_handle)
{
    bool valid = false;
    if (item_handle.idx > 0 && item_handle.idx <= this->capacity && this->items[item_handle.idx].in_use &&
        this->items[item_handle.idx].gen_id == item_handle.gen_id)
    {
        valid = true;
    }
    return valid;
}

template <typename T>
bool
ResourcePool<T>::item_from_handle(PoolHandle item_handle, T** out_value)
{
    bool success = false;
    ArrayItemHeader<T>* result = &this->items[0];
    if (is_handle_valid(item_handle))
    {
        result = &this->items[item_handle.idx];
        success = true;
    }
    *out_value = &result->data;
    return success;
}

template <typename T>
PoolHandle
ResourcePool<T>::item_new(T** out_item)
{
    PoolHandle handle = this->handle_get();
    *out_item = &this->items[handle.idx].data;
    return handle;
}

template <typename T>
PoolHandle
ResourcePool<T>::handle_get()
{
    AssertAlways(this->free_list != 0);

    U32 idx = this->free_list;

    ArrayItemHeader<T>* item = &this->items[idx];
    this->free_list = item->next;
    item->in_use = true;
    item->next = 0;

    this->item_in_use_count += 1;
    PoolHandle handle = {idx, this->items[idx].gen_id};
    return handle;
}

template <typename T>
void
ResourcePool<T>::item_free(PoolHandle item_handle)
{
    if (is_handle_valid(item_handle))
    {
        U32 item_idx = item_handle.idx;
        ArrayItemHeader<T>* item = &this->items[item_idx];
        if (item->constructed)
        {
            item->data.~T();
            item->constructed = false;
        }
        item->next = this->free_list;
        item->in_use = false;
        item->gen_id += 1;

        this->free_list = item_idx;
        this->item_in_use_count--;
    }
}

template <typename T>
void
ResourcePool<T>::reset_freelist()
{
    for (U32 i = 1; i < this->capacity; i++)
    {
        this->items[i].next = i + 1;
    }
    this->items[this->capacity].next = 0;
    this->free_list = 1;
}

template <typename T>
void
ResourcePool<T>::invalidate_all()
{
    for (U32 i = 0; i <= this->capacity; i++)
    {
        if (this->items[i].in_use)
        {
            this->item_free({i, this->items[i].gen_id});
        }
    }
    this->item_in_use_count = 0;
    this->reset_freelist();
}

template <typename T>
PoolIterator<T>
ResourcePool<T>::begin()
{
    PoolIterator<T> iter = {this, 1};
    iter.skip_unused();
    return iter;
}

template <typename T>
PoolIterator<T>
ResourcePool<T>::end()
{
    PoolIterator<T> iter = {this, this->capacity + 1};
    return iter;
}

template <typename T>
T&
PoolIterator<T>::operator*()
{
    Assert(this->pool);
    Assert(this->idx > 0 && this->idx <= this->pool->capacity);
    Assert(this->pool->items[this->idx].in_use);

    T& result = this->pool->items[this->idx].data;
    return result;
}

template <typename T>
T*
PoolIterator<T>::operator->()
{
    Assert(this->pool);
    Assert(this->idx > 0 && this->idx <= this->pool->capacity);
    Assert(this->pool->items[this->idx].in_use);

    T* result = &this->pool->items[this->idx].data;
    return result;
}

template <typename T>
PoolIterator<T>&
PoolIterator<T>::operator++()
{
    this->idx += 1;
    this->skip_unused();
    return *this;
}

template <typename T>
bool
PoolIterator<T>::operator!=(PoolIterator<T> other)
{
    bool result = this->pool != other.pool || this->idx != other.idx;
    return result;
}

template <typename T>
bool
PoolIterator<T>::operator==(PoolIterator<T> other)
{
    bool result = this->pool == other.pool && this->idx == other.idx;
    return result;
}

template <typename T>
void
PoolIterator<T>::skip_unused()
{
    Assert(this->pool);
    while (this->idx <= this->pool->capacity)
    {
        ArrayItemHeader<T>* item = &this->pool->items[this->idx];
        if (item->in_use)
        {
            break;
        }
        this->idx += 1;
    }
}

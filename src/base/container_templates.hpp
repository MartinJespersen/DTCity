#pragma once

#include "container.hpp"
#include "base_allocator_templates.hpp"
#include "base_profile.hpp"

// Template implementations
// Arrays
template <typename T>
Array<T>::Array(Allocator* allocator, U32 size) noexcept
{
    this->data = static_cast<T*>(allocator->push(sizeof(T) * size, alignof(T)));
    for (U32 i = 0; i < size; ++i)
    {
        new (&this->data[i]) T{};
    }
    this->size = size;
    this->type = AllocationType::Arena;
}

template <typename T>
Array<T>::Array(U32 size) noexcept
{
    this->size = size;
    this->data = new T[size];
    this->type = AllocationType::General;
}

template <typename T>
Array<T>::~Array() noexcept
{
    _array_release();
}

template <typename T>
Array<T>::Array(Array&& other) noexcept : Array()
{
    *this = std::move(other);
}

template <typename T>
Array<T>&
Array<T>::operator=(Array&& other) noexcept
{
    if (this != &other)
    {
        _array_release();
        data = std::exchange(other.data, nullptr);
        size = std::exchange(other.size, 0);
        type = std::exchange(other.type, AllocationType::Arena);
    }
    return *this;
}

template <typename T>
T*
Array<T>::begin() noexcept
{
    return data;
}

template <typename T>
const T*
Array<T>::begin() const noexcept
{
    return data;
}

template <typename T>
T*
Array<T>::end() noexcept
{
    return size ? data + size : data;
}

template <typename T>
const T*
Array<T>::end() const noexcept
{
    return size ? data + size : data;
}

template <typename T>
T&
Array<T>::operator[](U64 index) noexcept
{
    Assert(index < size);
    return data[index];
}

template <typename T>
const T&
Array<T>::operator[](U64 index) const noexcept
{
    Assert(index < size);
    return data[index];
}

// Buffers
template <typename T>
Buffer<T>
buffer_alloc(Arena* arena, U64 count)
{
    Buffer<T> buffer = {0};
    buffer.data = PushArray(arena, T, count);
    buffer.size = count;
    return buffer;
};

template <typename T>
void
buffer_copy(Buffer<T> dst, Buffer<T> src, U64 element_count_to_copy)
{
    Assert(dst.size >= element_count_to_copy);
    MemoryCopy(dst.data, src.data, element_count_to_copy * sizeof(T));
}

template <typename T>
void
BufferCopy(Buffer<T> dst, Buffer<T> src, U64 dst_offset, U64 src_offset, U64 size)
{
    Assert(dst.size >= dst_offset + size);
    MemoryCopy(dst.data + dst_offset, src.data + src_offset, size * sizeof(T));
}

template <typename T>
void
BufferItemRemove(Buffer<T>* in_out_buffer, U32 index)
{
    Assert(index < in_out_buffer->size);
    U32 type_size = sizeof(T);
    MemoryCopy(in_out_buffer->data + index, in_out_buffer->data + index + 1,
               type_size * (in_out_buffer->size - index - 1));
    in_out_buffer->size--;
}

template <typename T>
Buffer<T>
buffer_arena_copy(Arena* arena, Buffer<T> buffer)
{
    Buffer<T> new_buffer = buffer_alloc<T>(arena, buffer.size);
    MemoryCopy(new_buffer.data, buffer.data, buffer.size * sizeof(T));
    return new_buffer;
}

template <typename T>
Buffer<T>
buffer_concat(Arena* arena, Buffer<T> a, Buffer<T> b)
{
    Buffer<T> result = buffer_alloc<T>(arena, a.size + b.size);
    MemoryCopy(result.data, a.data, a.size * sizeof(T));
    MemoryCopy(result.data + a.size, b.data, b.size * sizeof(T));
    return result;
}

template <typename T>
Buffer<T>
buffer_from_arr(Arena* arena, T* arr, U64 size)
{
    Buffer<T> buffer = buffer_alloc<T>(arena, size);
    MemoryCopy(buffer.data, arr, size * sizeof(T));
    return buffer;
}

// Chunk lists
template <typename T>
ChunkList<T>*
chunk_list_create(Arena* arena, U64 capacity)
{
    ChunkList<T>* chunk = PushStruct(arena, ChunkList<T>);
    chunk->capacity = capacity;
    chunk->chunk_count = 0;
    chunk->total_count = 0;

    return chunk;
}

template <typename T>
ChunkItem<T>*
chunk_item_from_array(Arena* arena, T* values, U64 count)
{
    ChunkItem<T>* chunk = PushStruct(arena, ChunkItem<T>);
    chunk->values = values;
    chunk->count = count;
    return chunk;
}

template <typename T>
void
chunk_list_insert(Arena* arena, ChunkList<T>* list, T& item)
{
    T* res = chunk_list_get_next(arena, list);
    *res = item;
}

template <typename T>
ChunkItem<T>*
chunk_item_create(Arena* arena, ChunkList<T>* list)
{
    ChunkItem<T>* chunk = list->free_list;
    if (chunk)
    {
        chunk->count = 0;
        chunk->next;
        SLLStackPop(list->free_list);
    }
    else
    {
        chunk = PushStruct(arena, ChunkItem<T>);
        chunk->values = PushArray(arena, T, list->capacity);
    }
    return chunk;
}

template <typename T>
void
chunk_list_from_buffer_append(Arena* arena, ChunkList<T>* list, const Buffer<T>& buffer)
{
    Assert(list->capacity > 0);

    U64 offset = 0;
    while (offset < buffer.size)
    {
        ChunkItem<T>* chunk = list->last;
        if (!chunk || chunk->count >= list->capacity)
        {
            chunk = chunk_item_create(arena, list);
            SLLQueuePush(list->first, list->last, chunk);
            list->chunk_count += 1;
        }

        U64 available_count = list->capacity - chunk->count;
        U64 copy_count = Min(buffer.size - offset, available_count);

        MemoryCopy(chunk->values + chunk->count, buffer.data + offset, copy_count * sizeof(T));

        chunk->count += copy_count;
        offset += copy_count;
    }

    list->total_count += buffer.size;
}

template <typename T>
void
chunk_list_insert_chunk(ChunkList<T>* list, ChunkItem<T>* chunk)
{
    SLLQueuePush(list->first, list->last, chunk);
    list->chunk_count += 1;
    list->total_count += chunk->count;
}

template <typename T>
T*
chunk_list_get_next(Arena* arena, ChunkList<T>* list)
{
    ChunkItem<T>* chunk = list->last;
    if (!chunk || chunk->count >= list->capacity)
    {
        chunk = chunk_item_create(arena, list);
        SLLQueuePush(list->first, list->last, chunk);
        list->chunk_count += 1;
    }
    T* item = &chunk->values[chunk->count++];
    list->total_count++;
    return item;
}

template <typename T>
Buffer<T>
buffer_from_chunk_list(Arena* arena, ChunkList<T>* list)
{
    Buffer<T> buffer = buffer_alloc<T>(arena, list->total_count);
    U64 offset = 0;
    for (ChunkItem<T>* chunk = list->first; chunk; chunk = chunk->next)
    {
        MemoryCopy(buffer.data + offset, chunk->values, chunk->count * sizeof(T));
        offset += chunk->count;
    }
    return buffer;
}

template <typename T>
Buffer<T>
buffer_from_chunk_list_append(Buffer<T> buffer, U32 buffer_offset, ChunkList<T>* list)
{
    Assert(buffer_offset + list->total_count <= buffer.size);
    U64 offset = buffer_offset;
    for (ChunkItem<T>* chunk = list->first; chunk; chunk = chunk->next)
    {
        MemoryCopy(buffer.data + offset, chunk->values, chunk->count * sizeof(T));
        offset += chunk->count;
    }
    return buffer;
}

template <typename T>
void
chunk_list_empty(ChunkList<T>* list)
{
    ChunkItem<T>* free_list = list->free_list;
    while (list->first)
    {
        ChunkItem<T>* chunk = list->first;
        SLLStackPop(list->first);

        SLLStackPush(free_list, chunk);
    }
    U64 capacity = list->capacity;
    *list = {};
    list->free_list = free_list;
    list->capacity = capacity;
}

// Linked list maps
template <typename K, typename V>
Map<K, V>::Map(Allocator* allocator, U64 bucket_capacity)
{
    this->init(allocator->arena, bucket_capacity);
}

template <typename K, typename V>
Map<K, V>*
Map<K, V>::create(Arena* arena, U64 bucket_capacity)
{
    using MapType = Map<K, V>;
    Map* map = PushStruct(arena, MapType);
    map->init(arena, bucket_capacity);
    return map;
}

template <typename K, typename V>
void
Map<K, V>::init(Arena* arena, U64 bucket_capacity)
{
    static_assert(std::is_trivially_copyable_v<K> && std::is_trivially_copyable_v<V>);
    static_assert(std::is_trivially_destructible_v<K> && std::is_trivially_destructible_v<V>);
    AssertAlways(bucket_capacity <= (U64(1) << 63));
    U64 actual_capacity = _round_up_pow2_u64(bucket_capacity);
    AssertAlways(actual_capacity <= U64(-1) / sizeof(MapChunkList<K, V>));
    Assert(!v);
    using Bucket = MapChunkList<K, V>;
    v = PushArray(arena, Bucket, actual_capacity);
    capacity = actual_capacity;
}

template <typename K, typename V>
void
Map<K, V>::clear()
{
    for (U64 index = 0; index < capacity; ++index)
    {
        for (MapChunk<K, V>* chunk = v[index].first; chunk; chunk = chunk->next)
        {
            chunk->count = 0;
        }
        v[index].total_count = 0;
    }
}

template <typename K, typename V>
V*
Map<K, V>::get(K key)
{
    if (capacity)
    {
        U64 hash = _hash_u64((U64)key);
        U64 index = hash % capacity;
        for (MapChunk<K, V>* chunk = v[index].first; chunk; chunk = chunk->next)
        {
            for (U64 i = 0; i < chunk->count; ++i)
            {
                if (chunk->v[i].key == key)
                    return &chunk->v[i].value;
            }
        }
    }
    return nullptr;
}

template <typename K, typename V>
MapResult
Map<K, V>::get(K key, V** out_value)
{
    *out_value = get(key);
    return *out_value ? MapResult::Success : MapResult::NotFound;
}

template <typename K, typename V>
V*
Map<K, V>::insert(Arena* arena, K key, const V& value)
{
    if (!capacity)
        init(arena, 8);

    U64 hash = _hash_u64((U64)key);
    U64 index = hash % capacity;
    MapChunkList<K, V>* chunk_list = &v[index];
    for (MapChunk<K, V>* chunk = chunk_list->first; chunk; chunk = chunk->next)
    {
        for (U64 i = 0; i < chunk->count; ++i)
        {
            if (chunk->v[i].key == key)
                return nullptr;
        }
    }

    // Reuse existing chunks after clear() before allocating another one.
    MapChunk<K, V>* chunk = chunk_list->first;
    while (chunk && chunk->count == ArrayCount(chunk->v))
        chunk = chunk->next;
    if (!chunk)
    {
        using Chunk = MapChunk<K, V>;
        chunk = PushStruct(arena, Chunk);
        SLLQueuePush(chunk_list->first, chunk_list->last, chunk);
        chunk_list->chunk_count += 1;
    }

    U64 i = chunk->count;
    chunk->v[i].key = key;
    chunk->v[i].value = value;
    chunk_list->total_count += 1;
    chunk->count += 1;
    return &chunk->v[i].value;
}

template <typename K, typename V>
U64
Map<K, V>::_hash_u64(U64 x)
{
    prof_scope_marker;
    String8 str = {.str = (U8*)&x, .size = sizeof(U64)};
    U64 res = hash_u128_from_str8(str).u64[1];
    return res;
}

template <typename K, typename V>
U64
Map<K, V>::_round_up_pow2_u64(U64 v)
{
    if (v <= 8)
        return 8;
    v -= 1;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v |= v >> 32;
    return v + 1;
}

template <typename T>
void
Array<T>::_array_release() noexcept
{
    if (type == AllocationType::General)
        delete[] data;
    else
    {
        // The array owns element lifetimes; the allocator owns their storage.
        for (U32 i = size; i > 0; --i)
            data[i - 1].~T();
    }
    data = nullptr;
    size = 0;
    type = AllocationType::Arena;
}

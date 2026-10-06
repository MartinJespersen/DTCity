////////////////////////////////

#pragma once

#include <initializer_list>

template <typename T>
struct Result
{
    T v;
    B32 err;
};

template <typename T>
Result<T>
result(T v, B32 err)
{
    return {v, err};
}

template <typename T>
Result<T>
result_ok(T v)
{
    return {v, false};
};

template <typename T>
Result<T>
result_not_ok(T v)
{
    return {v, true};
};

// ~Array
enum class AllocationType : U32
{
    Arena,
    General
};

template <typename T>
struct Array
{
    T* data = {};
    U32 size = {};
    AllocationType type = AllocationType::Arena;

    Array() = default;
    // This array must be destroyed before its backing allocator is cleared or released.
    Array(Allocator* allocator, U32 size) noexcept;
    Array(U32 size) noexcept;
    ~Array() noexcept;

    Array(const Array&) = delete;
    Array&
    operator=(const Array&) = delete;

    Array(Array&& other) noexcept;
    Array&
    operator=(Array&& other) noexcept;

    T*
    begin() noexcept;
    const T*
    begin() const noexcept;
    T*
    end() noexcept;
    const T*
    end() const noexcept;
    T&
    operator[](U64 index) noexcept;
    const T&
    operator[](U64 index) const noexcept;

  private:
    void
    _array_release() noexcept;
};

template <typename T>
struct Buffer
{
    T* data;
    U64 size;

    T&
    operator[](U64 index)
    {
        return data[index];
    }

    T*
    begin()
    {
        return data;
    }

    T*
    end()
    {
        return data + size;
    }
};

template <typename T>
Buffer<T>
buffer_alloc(Arena* arena, U64 count);
template <typename T>
Buffer<T>
buffer_from_arr(Arena* arena, T* arr, U64 size);
template <typename T>
void
buffer_copy(Buffer<T> dst, Buffer<T> src, U64 element_count_to_copy);
template <typename T>
void
BufferCopy(Buffer<T> dst, Buffer<T> src, U64 dst_offset, U64 src_offset, U64 size);
template <typename T>
void
BufferItemRemove(Buffer<T>* in_out_buffer, U32 index);
template <typename T>
Buffer<T>
buffer_arena_copy(Arena* arena, Buffer<T> buffer);
template <typename T>
Buffer<T>
buffer_concat(Arena* arena, Buffer<T> a, Buffer<T> b);
Buffer<String8>
Str8BufferFromCString(Arena* arena, std::initializer_list<const char*> strings);
String8
str8_path_from_str8_list(Arena* arena, std::initializer_list<String8> strings);
String8
CreatePathFromStrings(Arena* arena, Buffer<String8> path_elements);

////////////////////////////////////////////////////////
namespace io
{
Buffer<U8>
file_read(Arena* arena, String8 filename);
}

char**
CStrArrFromStr8Buffer(Arena* arena, Buffer<String8> buffer);

// ~mgj: ChunkList
template <typename T>
struct ChunkItem
{
    ChunkItem<T>* next;
    T* values;
    U64 count;
};

template <typename T>
struct ChunkList
{
    ChunkItem<T>* first;
    ChunkItem<T>* last;
    ChunkItem<T>* free_list;
    U64 capacity;
    U64 chunk_count;
    U64 total_count;

    T&
    operator[](U64 idx)
    {
        U64 chunk_idx = idx / capacity;
        U64 value_idx = idx % capacity;

        ChunkItem<T>* chunk = first;
        for (U64 i = 0; i < chunk_idx; ++i)
        {
            chunk = chunk->next;
        }

        return chunk->values[value_idx];
    }

    struct Iterator
    {
        ChunkItem<T>* cur_item;
        U64 cur_val_idx;

        Iterator(ChunkItem<T>* item, U64 val_idx)
        {
            cur_item = item;
            cur_val_idx = val_idx;
        }

        Iterator&
        operator++()
        {
            ++cur_val_idx;
            if (cur_val_idx >= cur_item->count && cur_item->next)
            {
                cur_item = cur_item->next;
                cur_val_idx = 0;
            }
            return *this;
        }

        T&
        operator*()
        {
            return cur_item->values[cur_val_idx];
        }

        bool
        operator==(const Iterator& other) const
        {
            return cur_item == other.cur_item && cur_val_idx == other.cur_val_idx;
        }

        bool
        operator!=(const Iterator& other) const
        {
            return !(*this == other);
        }
    };

    Iterator
    begin()
    {
        return Iterator(first, 0);
    }
    Iterator
    end()
    {
        return Iterator(last, last ? last->count : 0);
    }
};

template <typename T>
ChunkList<T>*
chunk_list_create(Arena* arena, U64 capacity);
template <typename T>
ChunkItem<T>*
chunk_item_create(Arena* arena, ChunkList<T>* list);

template <typename T>
void
chunk_list_from_buffer_append(Arena* arena, ChunkList<T>* list, const Buffer<T>& buffer);
template <typename T>
void
chunk_list_empty(ChunkList<T>* list);
template <typename T>
ChunkItem<T>*
chunk_item_from_array(Arena* arena, T* values, U64 count);
template <typename T>
void
chunk_list_insert(Arena* arena, ChunkList<T>* list, T& item);
template <typename T>
void
chunk_list_insert_chunk(ChunkList<T>* list, ChunkItem<T>* chunk);
template <typename T>
T*
chunk_list_get_next(Arena* arena, ChunkList<T>* list);
template <typename T>
Buffer<T>
buffer_from_chunk_list(Arena* arena, ChunkList<T>* list);
template <typename T>
Buffer<T>
buffer_from_chunk_list_append(Buffer<T> buffer, U32 buffer_offset, ChunkList<T>* list);

String8
str8_from_chunk_list(Arena* arena, ChunkList<U8>* list);
// defer implementation
template <typename F>
struct privDefer
{
    F f;
    privDefer(F f) : f(f)
    {
    }
    ~privDefer()
    {
        f();
    }
};

template <typename F>
privDefer<F>
defer_func(F f)
{
    return privDefer<F>(f);
}

#define DEFER_1(x, y) x##y
#define DEFER_2(x, y) DEFER_1(x, y)
#define DEFER_3(x) DEFER_2(x, __COUNTER__)
#define defer(code) auto DEFER_3(_defer_) = defer_func([&]() { code; })

// Linked List Map
template <typename K, typename T>
struct MapItem
{
    K key;
    T value;
};

const U64 DEFAULT_MAP_CHUNK_SIZE = 14;
template <typename K, typename V, U64 N = DEFAULT_MAP_CHUNK_SIZE>
struct MapChunk
{
    static_assert(sizeof(K) == sizeof(void*), "Key size must match pointer size");
    MapChunk<K, V, N>* next;
    U64 count;
    MapItem<K, V> v[N];
};

template <typename K, typename T, U64 N = DEFAULT_MAP_CHUNK_SIZE>
struct MapChunkList
{
    MapChunk<K, T, N>* first;
    MapChunk<K, T, N>* last;
    U64 chunk_count;
    U64 total_count;
};

enum class MapResult : B32
{
    Success = 0,
    NotFound = 1
};

// The caller owns the arena; use the same arena for initialization and insertion.
// Values must not require destruction. clear() retains chunks for reuse.
// Value pointers remain valid until clear() or the backing arena is reset/released.
template <typename K, typename V>
struct Map
{
    MapChunkList<K, V>* v = {};
    U64 capacity = {};

    Map(Allocator* allocator, U64 bucket_capacity);
    static Map*
    create(Arena* arena, U64 bucket_capacity);
    void
    init(Arena* arena, U64 bucket_capacity);
    void
    clear();
    V*
    get(K key);
    MapResult
    get(K key, V** out_value);
    // Duplicate keys leave the existing value unchanged and return nullptr.
    V*
    insert(Arena* arena, K key, const V& value);

  private:
    static U64
    _hash_u64(U64 value);
    static U64
    _round_up_pow2_u64(U64 value);
};

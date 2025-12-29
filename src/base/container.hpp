////////////////////////////////

#pragma once

#include <initializer_list>

template <typename T> struct Result
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

template <typename T> struct Buffer
{
    T* data;
    U64 size;

    T*
    operator[](U64 index)
    {
        return &data[index];
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
BufferAlloc(Arena* arena, U64 count);
template <typename T>
static void
BufferCopy(Buffer<T> dst, Buffer<T> src, U64 element_count_to_copy);
template <typename T>
static void
BufferCopy(Buffer<T> dst, Buffer<T> src, U64 dst_offset, U64 src_offset, U64 size);
template <typename T>
static void
BufferItemRemove(Buffer<T>* in_out_buffer, U32 index);
template <typename T>
static void
BufferAppend(Buffer<T> buffer, Buffer<T> buffer_append, U32 append_idx);
template <typename T>
static Buffer<T>
buffer_arena_copy(Arena* arena, Buffer<T> buffer);

static Buffer<String8>
Str8BufferFromCString(Arena* arena, std::initializer_list<const char*> strings);
static String8
Str8PathFromStr8List(Arena* arena, std::initializer_list<String8> strings);
static String8
CreatePathFromStrings(Arena* arena, Buffer<String8> path_elements);

////////////////////////////////////////////////////////
static Buffer<U8>
io_file_read(Arena* arena, String8 filename);

static char**
CStrArrFromStr8Buffer(Arena* arena, Buffer<String8> buffer);

// ~mgj: ChunkList
template <typename T> struct ChunkItem
{
    ChunkItem<T>* next;
    T* values;
    U64 count;
};

template <typename T> struct ChunkList
{
    ChunkItem<T>* first;
    ChunkItem<T>* last;
    U64 capacity;
    U64 chunk_count;
    U64 total_count;

    T&
    operator[](U64 idx)
    {
        U64 chunk_idx = idx / capacity;

        ChunkItem<T>* chunk = first;
        for (U64 i = 0; i < chunk_idx; ++i)
        {
            chunk = chunk->next;
        }

        return chunk->values[chunk->count - 1];
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
void
chunk_list_insert(Arena* arena, ChunkList<T>* list, T& item);
template <typename T>
T*
chunk_list_get_next(Arena* arena, ChunkList<T>* list);
template <typename T>
Buffer<T>
buffer_from_chunk_list(Arena* arena, ChunkList<T>* list);

// defer implementation
template <typename F> struct privDefer
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
template <typename K, typename T> struct MapItem
{
    K key;
    T value;
};

const U64 DEFAULT_MAP_CHUNK_SIZE = 14;
template <typename K, typename V, U64 N = DEFAULT_MAP_CHUNK_SIZE> struct MapChunk
{
    static_assert(sizeof(K) == sizeof(void*), "Key size must match pointer size");
    MapChunk<K, V, N>* next;
    U64 count;
    MapItem<K, V> v[N];
};

template <typename K, typename T, U64 N = DEFAULT_MAP_CHUNK_SIZE> struct MapChunkList
{
    MapChunk<K, T, N>* first;
    MapChunk<K, T, N>* last;
    U64 chunk_count;
    U64 total_count;
};

template <typename K, typename V> struct Map
{
    Arena* arena;
    MapChunkList<K, V>* v;
    U64 capacity;
};

enum class MapResult : B32
{
    Ok = 0,
    NotFound = 1
};

template <typename K, typename V>
static Map<K, V>*
map_create(Arena* arena, U64 capacity);

template <typename K, typename V>
static V*
map_get(Map<K, V>* m, K key);

template <typename K, typename V>
static MapResult
map_get(Map<K, V>* m, K key, V** out_value);

template <typename K, typename V>
static V*
map_insert(Map<K, V>* m, K key, V& value);

// private map functions
static inline U64
map_hash_u64(U64 x);

static inline U64
map_round_up_pow2_u64(U64 v);

/////////////////////////////////////////////////////

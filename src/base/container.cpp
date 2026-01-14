////////////////////////////////
//~mgj: Container

template <typename T>
Buffer<T>
BufferAlloc(Arena* arena, U64 count)
{
    Buffer<T> buffer = {0};
    buffer.data = PushArray(arena, T, count);
    buffer.size = count;
    return buffer;
};

template <typename T>
static void
BufferCopy(Buffer<T> dst, Buffer<T> src, U64 element_count_to_copy)
{
    Assert(dst.size >= element_count_to_copy);
    MemoryCopy(dst.data, src.data, element_count_to_copy * sizeof(T));
}

template <typename T>
static void
BufferCopy(Buffer<T> dst, Buffer<T> src, U64 dst_offset, U64 src_offset, U64 size)
{
    Assert(dst.size >= dst_offset + size);
    MemoryCopy(dst.data + dst_offset, src.data + src_offset, size * sizeof(T));
}

template <typename T>
static void
BufferItemRemove(Buffer<T>* in_out_buffer, U32 index)
{
    Assert(index < in_out_buffer->size);
    U32 type_size = sizeof(T);
    MemoryCopy(in_out_buffer->data + index, in_out_buffer->data + index + 1,
               type_size * (in_out_buffer->size - index - 1));
    in_out_buffer->size--;
}

template <typename T>
static Buffer<T>
buffer_arena_copy(Arena* arena, Buffer<T> buffer)
{
    Buffer<T> new_buffer = BufferAlloc<T>(arena, buffer.size);
    MemoryCopy(new_buffer.data, buffer.data, buffer.size * sizeof(T));
    return new_buffer;
}

template <typename T>
static Buffer<T>
buffer_concat(Arena* arena, Buffer<T> a, Buffer<T> b)
{
    Buffer<T> result = BufferAlloc<T>(arena, a.size + b.size);
    MemoryCopy(result.data, a.data, a.size * sizeof(T));
    MemoryCopy(result.data + a.size, b.data, b.size * sizeof(T));
    return result;
}

////////////////////////////////
static Buffer<String8>
Str8BufferFromCString(Arena* arena, std::initializer_list<const char*> strings)
{
    Buffer<String8> buffer = BufferAlloc<String8>(arena, strings.size());
    U32 index = 0;
    for (const char* const* str = strings.begin(); str != strings.end(); ++str)
    {
        buffer.data[index++] = push_str8_copy(arena, str8_c_string(*str));
    }
    return buffer;
}

static String8
str8_path_from_str8_list(Arena* arena, std::initializer_list<String8> strings)
{
    String8List path_list = {0};
    StringJoin join_params = {.sep = os_path_delimiter()};
    for (const String8* str = strings.begin(); str != strings.end(); ++str)
    {
        str8_list_push(arena, &path_list, *str);
    }
    String8 result = str8_list_join(arena, &path_list, &join_params);
    return result;
}

static String8
CreatePathFromStrings(Arena* arena, Buffer<String8> path_elements)
{
    String8List path_list = {0};
    StringJoin join_params = {.sep = os_path_delimiter()};

    // Step 1: Convert each char* to String8 and push to list
    for (U64 i = 0; i < path_elements.size; i++)
    {
        String8 part = path_elements.data[i];
        str8_list_push(arena, &path_list, part);
    }

    String8 result = str8_list_join(arena, &path_list, &join_params);
    return result;
}

namespace io
{

static Buffer<U8>
file_read(Arena* arena, String8 filename)
{
    Buffer<U8> buffer = {0};
    FILE* file = fopen((const char*)filename.str, "rb");
    defer(fclose(file));
    Assert(file != nullptr);
    if (file == NULL)
    {
        DEBUG_LOG("failed to open file!");

        return buffer;
    }

    fseek(file, 0, SEEK_END);
    buffer.size = (U64)ftell(file);
    fseek(file, 0, SEEK_SET);

    buffer.data = PushArray(arena, U8, buffer.size);
    fread(buffer.data, sizeof(U8), buffer.size, file);

    return buffer;
}

} // namespace io

//~mgj: Strings functions

static char**
CStrArrFromStr8Buffer(Arena* arena, Buffer<String8> buffer)
{
    char** arr = PushArray(arena, char*, buffer.size);

    for (U32 i = 0; i < buffer.size; i++)
    {
        arr[i] = (char*)buffer.data[i].str;
    }
    return arr;
}

//~mgj: ChunckList: safe T value in contigous Chunks usually for intermediate storage
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
void
chunk_list_insert(Arena* arena, ChunkList<T>* list, T& item)
{
    T* res = chunk_list_get_next(arena, list);
    *res = item;
}

template <typename T>
T*
chunk_list_get_next(Arena* arena, ChunkList<T>* list)
{
    ChunkItem<T>* chunk = list->last;
    if (!chunk || chunk->count >= list->capacity)
    {
        chunk = PushStruct(arena, ChunkItem<T>);
        SLLQueuePush(list->first, list->last, chunk);
        chunk->values = PushArray(arena, T, list->capacity);
        list->chunk_count += 1;
    }
    T* item = &chunk->values[chunk->count++];
    list->total_count++;
    return item;
}

template <typename T>
static Buffer<T>
buffer_from_chunk_list(Arena* arena, ChunkList<T>* list)
{
    Buffer<T> buffer = BufferAlloc<T>(arena, list->total_count);
    U64 offset = 0;
    for (ChunkItem<T>* chunk = list->first; chunk; chunk = chunk->next)
    {
        MemoryCopy(buffer.data + offset, chunk->values, chunk->count * sizeof(T));
        offset += chunk->count;
    }
    return buffer;
}

// Linked List Map
static inline U64
map_hash_u64(U64 x)
{
    prof_scope_marker;
    String8 str = {.str = (U8*)&x, .size = sizeof(U64)};
    U64 res = hash_u128_from_str8(str).u64[1];
    return res;
}

static inline U64
map_round_up_pow2_u64(U64 v)
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

template <typename K, typename V>
static Map<K, V>*
map_create(Arena* arena, U64 capacity)
{
    using MapType = Map<K, V>;
    Map<K, V>* map = PushStruct(arena, MapType);
    map->arena = arena;
    U64 actual_cap = map_round_up_pow2_u64(capacity);
    using MapKeyValuePairList = MapChunkList<K, V>;
    map->v = PushArray(arena, MapKeyValuePairList, actual_cap);
    map->capacity = actual_cap;
    return map;
}

template <typename K, typename V>
static V*
map_get(Map<K, V>* m, K key)
{
    U64 hash = map_hash_u64(key);
    U64 index = hash % m->capacity;
    MapChunkList<K, V>* chunk_list = &m->v[index];
    MapChunk<K, V>* chunk = chunk_list->first;
    while (chunk)
    {
        for (U64 i = 0; i < chunk->count; ++i)
        {
            if (chunk->v[i].key == key)
                return &chunk->v[i].value;
        }
        chunk = chunk->next;
    }
    return nullptr;
}

template <typename K, typename V>
static MapResult
map_get(Map<K, V>* m, K key, V** out_value)
{
    V* value = map_get(m, key);
    if (value)
    {
        *out_value = value;
        return MapResult::Ok;
    }

    *out_value = nullptr;
    return MapResult::NotFound;
}

template <typename K, typename V>
static V*
map_insert(Map<K, V>* m, K key, V& value)
{
    using KeyPair = MapChunk<K, V>;

    U64 hash = map_hash_u64((U64)key);
    U64 index = hash % m->capacity;
    MapChunkList<K, V>* chunk_list = &m->v[index];
    MapChunk<K, V>* chunk = chunk_list->first;

    // check if key is already present
    for (; chunk; chunk = chunk->next)
    {
        for (U64 i = 0; i < chunk->count; ++i)
        {
            if (chunk->v[i].key == key)
                return nullptr;
        }
    }

    U64 i = 0;
    if (!chunk || chunk->count >= ArrayCount(chunk->v))
    {
        chunk = PushStruct(m->arena, KeyPair);
        SLLQueuePush(chunk_list->first, chunk_list->last, chunk);
        chunk_list->chunk_count += 1;
    }

    chunk = chunk_list->last;
    chunk->v[i].key = key;
    chunk->v[i].value = value;
    chunk_list->total_count += 1;
    chunk->count += 1;

    return &chunk->v[i].value;
}

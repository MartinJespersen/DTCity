// Pow2 Free list
lib_internal Pow2Freelist*
pow2_freelist_create(Arena* arena, U32 exponent_min, U32 exponent_count)
{
    Pow2Freelist* pool = PushStruct(arena, Pow2Freelist);
    Debug_SetName(arena, "dynamic array pool arena");
    using NodePtr = Pow2FreelistNode*;
    pool->exponent_min = exponent_min;
    pool->exponent_count = exponent_count;
    pool->free_list = PushArray(arena, NodePtr, exponent_count + 1);

    return pool;
}

template <typename T>
lib_internal BufferHandle<T>
buffer_from_pow2_freelist(Arena* arena, Pow2Freelist* freelist, U64 count)
{
    U64 element_byte_size = sizeof(T);
    U64 requested_capacity = Max(count, 1ULL);
    Assert(requested_capacity <= UINT64_MAX / element_byte_size);

    U64 arr_offset = _pow2_freelist_offset_calc<T>();
    U64 requested_byte_size = requested_capacity * element_byte_size;
    U64 alloc_size = _pow2_freelist_alloc_size_find(requested_byte_size, arr_offset, freelist->exponent_min);
    U32 free_list_idx = _pow2_freelist_idx_from_alloc_size(alloc_size, freelist->exponent_min, freelist->exponent_count);

    Pow2FreelistNode* dyn_arr_node = freelist->free_list[free_list_idx];
    if (dyn_arr_node)
    {
        SLLStackPop(freelist->free_list[free_list_idx]);
    }
    else
    {
        dyn_arr_node = (Pow2FreelistNode*)PushArrayNoZeroAligned(arena, U8, alloc_size, POW2_FREELIST_ALIGN);
        dyn_arr_node->next = nullptr;
        dyn_arr_node->gen_id = 1;
    }

    T* data = (T*)((U8*)dyn_arr_node + arr_offset);
    MemoryZero(data, requested_byte_size);

    BufferHandle<T> buffer_handle = BufferHandle(data, requested_capacity, dyn_arr_node->gen_id);

    return buffer_handle;
}

template <typename T>
lib_internal bool
buffer_push_to_freelist(Pow2Freelist* freelist, BufferHandle<T>& handle)
{
    bool result = false;
    if (handle.gen_id)
    {
        U64 arr_offset = _pow2_freelist_offset_calc<T>();
        U64 alloc_size = _pow2_freelist_alloc_size_find(handle.buffer.size * sizeof(T), arr_offset, freelist->exponent_min);
        U64 free_list_idx = _pow2_freelist_idx_from_alloc_size(alloc_size, freelist->exponent_min, freelist->exponent_count);

        Pow2FreelistNode* dyn_arr_node = (Pow2FreelistNode*)((U8*)handle.buffer.data - arr_offset);
        if (handle.gen_id == dyn_arr_node->gen_id)
        {
            SLLStackPush(freelist->free_list[free_list_idx], dyn_arr_node);
            dyn_arr_node->gen_id += 1;
            if (dyn_arr_node->gen_id == 0)
            {
                dyn_arr_node->gen_id = 1;
            }
            handle.buffer = {};
            handle.gen_id = 0;
            result = true;
        }
    }
    return result;
}

template <typename T>
lib_internal BufferHandle<T>
buffer_handle_from_chunk_list(Arena* arena, Pow2Freelist* freelist, ChunkList<T>* list)
{
    BufferHandle<T> buffer_handle = buffer_from_pow2_freelist<T>(arena, freelist, list->total_count);
    Buffer<T> buffer = {};
    if (buffer_handle.buffer_try_get(&buffer))
    {
        U64 offset = 0;
        for (ChunkItem<T>* chunk = list->first; chunk; chunk = chunk->next)
        {
            MemoryCopy(buffer.data + offset, chunk->values, chunk->count * sizeof(T));
            offset += chunk->count;
        }
    }
    return buffer_handle;
}

lib_internal U64
_pow2_freelist_alloc_size_find(U64 size, U64 arr_offset, U32 exponent_min)
{
    Assert(exponent_min < 64);
    Assert(size <= UINT64_MAX - arr_offset);
    U64 full_size = arr_offset + size;
    U64 min_alloc_size = 1ULL << exponent_min;
    U64 alloc_size = Max(min_alloc_size, u64_up_to_pow2(full_size));
    return alloc_size;
}

template <typename T>
lib_internal U32
_pow2_freelist_offset_calc()
{
    Assert(Max(8, AlignOf(T)) <= POW2_FREELIST_ALIGN);
    return POW2_FREELIST_ALIGN;
}

lib_internal U32
_pow2_freelist_idx_from_alloc_size(U64 alloc_size, U32 exponent_min, U32 exponent_count)
{
    U32 alloc_idx = (U32)msb_index(alloc_size);
    Assert(alloc_idx <= exponent_min + exponent_count);
    U32 min_idx = Min(alloc_idx, exponent_min);
    U32 free_list_idx = alloc_idx - min_idx;
    return free_list_idx;
}

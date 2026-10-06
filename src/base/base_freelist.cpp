// Pow2 Free list
Pow2Freelist*
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

U64
_pow2_freelist_alloc_size_find(U64 size, U64 arr_offset, U32 exponent_min)
{
    Assert(exponent_min < 64);
    Assert(size <= UINT64_MAX - arr_offset);
    U64 full_size = arr_offset + size;
    U64 min_alloc_size = 1ULL << exponent_min;
    U64 alloc_size = Max(min_alloc_size, u64_up_to_pow2(full_size));
    return alloc_size;
}

U32
_pow2_freelist_idx_from_alloc_size(U64 alloc_size, U32 exponent_min, U32 exponent_count)
{
    U32 alloc_idx = (U32)msb_index(alloc_size);
    Assert(alloc_idx <= exponent_min + exponent_count);
    U32 min_idx = Min(alloc_idx, exponent_min);
    U32 free_list_idx = alloc_idx - min_idx;
    return free_list_idx;
}

namespace
{
// Dynamic Array
lib_internal void
dynamic_array_init(U64 bytes_to_reserve)
{
    AssertAlways(!g_dynamic_array_pool);

    ArenaParams arena_params = {};
    arena_params.reserve_size = u64_up_to_pow2(bytes_to_reserve);
    arena_params.commit_size = arena_default_commit_size;
    arena_params.flags = arena_default_flags;
    Arena* arena = arena_alloc(&arena_params);
    g_dynamic_array_pool = PushStruct(arena, DynamicArrayPool);
    g_dynamic_array_pool->mutex = OS_MutexAlloc();
    g_dynamic_array_pool->arena = arena;
    using NodePtr = DynamicArrayNode*;
    g_dynamic_array_pool->free_list = PushArray(arena, NodePtr, g_dynamic_array_max_idx - g_dynamic_array_min_idx + 1);
}

lib_internal void
dynamic_array_release()
{
    OS_MutexRelease(g_dynamic_array_pool->mutex);
    arena_release(g_dynamic_array_pool->arena);
}

template <typename T>
lib_internal DynamicArray<T>
dynamic_array_create(U64 start_capacity)
{
    U64 element_byte_size = sizeof(T);
    U64 requested_capacity = Max(start_capacity, 1ULL);
    Assert(requested_capacity <= UINT64_MAX / element_byte_size);

    U64 arr_offset = _dynamic_array_offset_calc<T>();
    U64 alloc_size = _dynamic_array_alloc_size_find(requested_capacity, element_byte_size, arr_offset);
    U64 free_list_idx = _free_list_idx_from_alloc_size(alloc_size);

    AssertAlways(g_dynamic_array_pool->arena);

    os_mutex_take(g_dynamic_array_pool->mutex);
    DynamicArrayNode* dyn_arr_node = g_dynamic_array_pool->free_list[free_list_idx];
    if (dyn_arr_node)
    {
        SLLStackPop(g_dynamic_array_pool->free_list[free_list_idx]);
        MemoryZero(dyn_arr_node, sizeof(DynamicArrayNode));
    }
    else
    {
        dyn_arr_node = (DynamicArrayNode*)PushArrayAligned(g_dynamic_array_pool->arena, U8, alloc_size, g_dynamic_array_max_align);
    }
    os_mutex_drop(g_dynamic_array_pool->mutex);

    T* data = (T*)((U8*)dyn_arr_node + arr_offset);

    U64 actual_capacity = (alloc_size - arr_offset) / element_byte_size;
    DynamicArray<T> dyn_arr = {};
    dyn_arr.data = data;
    dyn_arr.size = 0;
    dyn_arr.capacity = actual_capacity;
    return dyn_arr;
}

lib_internal U64
_dynamic_array_alloc_size_find(U64 elem_count, U64 elem_size, U64 arr_offset)
{
    Assert(elem_count <= UINT64_MAX / elem_size);
    U64 total_byte_array_capacity = elem_size * elem_count;
    Assert(total_byte_array_capacity <= UINT64_MAX - arr_offset);

    U64 full_size = arr_offset + total_byte_array_capacity;
    U64 min_alloc_size = 1ULL << g_dynamic_array_min_idx;
    U64 alloc_size = Max(min_alloc_size, u64_up_to_pow2(full_size));
    return alloc_size;
}

template <typename T>
lib_internal U64
_dynamic_array_offset_calc()
{
    U64 align = Max(8, AlignOf(T));
    Assert(align <= g_dynamic_array_max_align);
    U64 arr_offset = align_pow2(sizeof(DynamicArrayNode), align);
    return arr_offset;
}

lib_internal U32
_free_list_idx_from_alloc_size(U64 alloc_size)
{
    U32 alloc_idx = (U32)msb_index(alloc_size);
    Assert(alloc_idx <= g_dynamic_array_max_idx);
    U32 free_list_idx = alloc_idx - g_dynamic_array_min_idx;
    return free_list_idx;
}

template <typename T>
lib_internal void
dynamic_array_destroy(DynamicArray<T>* arr)
{
    if (arr->data)
    {
        U64 element_byte_size = sizeof(T);
        U64 arr_offset = _dynamic_array_offset_calc<T>();
        U64 alloc_size = _dynamic_array_alloc_size_find(arr->capacity, element_byte_size, arr_offset);

        U64 free_list_idx = _free_list_idx_from_alloc_size(alloc_size);

        DynamicArrayNode* dyn_arr_node = (DynamicArrayNode*)((U8*)arr->data - arr_offset);
        os_mutex_take(g_dynamic_array_pool->mutex);
        SLLStackPush(g_dynamic_array_pool->free_list[free_list_idx], dyn_arr_node);
        os_mutex_drop(g_dynamic_array_pool->mutex);
    }
}

template <typename T>
lib_internal void
dynamic_array_append(DynamicArray<T>* arr, Buffer<T> buffer)
{
    U64 new_size = buffer.size + arr->size;
    if (new_size > arr->capacity)
    {
        // U64 arr_offset = _dynamic_array_offset_calc<T>();
        // U64 alloc_size = _dynamic_array_alloc_size_find(new_size, sizeof(T), arr_offset);
        DynamicArray<T> new_dyn_array = dynamic_array_create<T>(new_size);

        MemoryCopy(new_dyn_array.data, arr->data, arr->size * sizeof(T));
        new_dyn_array.size = arr->size;

        dynamic_array_destroy(arr);
        *arr = static_cast<DynamicArray<T>&&>(new_dyn_array);
    }

    MemoryCopy(arr->data + arr->size, buffer.data, buffer.size * sizeof(T));
    arr->size = new_size;
}

template <typename T>
lib_internal void
dynamic_array_from_buffer_overwrite(DynamicArray<T>* arr, Buffer<T> buffer)
{
    arr->size = 0;
    dynamic_array_append(arr, buffer);
}

///////////////////////////////////////////////////////////////////////////////////////
// Arena Array

template <typename T>
ArenaArray<T>::ArenaArray(U64 max_capacity) noexcept
{
    U64 element_byte_size = sizeof(T);
    // ARENA_HEADER_SIZE have
    U64 reserve_arr_byte_size = align_pow2(element_byte_size * max_capacity + ARENA_HEADER_SIZE, KB(4));
    ArenaParams arena_params = {};
    arena_params.reserve_size = Max(reserve_arr_byte_size, KB(4));
    arena_params.commit_size = KB(4);
    arena_params.flags = ArenaFlag_NoChain;
    Arena* arena = arena_alloc(&arena_params);
    this->_arena = arena;
    this->_arr = (T*)((U8*)arena + arena_pos(arena));
    this->_capacity = (reserve_arr_byte_size - ARENA_HEADER_SIZE) / element_byte_size;
    this->size = 0;
}

template <typename T>
ArenaArray<T>::~ArenaArray<T>()
{
    arena_release(_arena);
}

template <typename T>
void
ArenaArray<T>::release(ArenaArray<T>* arr) noexcept
{
    ~ArenaArray(arr->_arena);
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
/////////////////////////////////////////////////////////////////////////
// Resource Pool
template <typename T>
g_internal ResourcePool<T>*
resource_pool_init(U64 reserve_element_size)
{
    U64 element_byte_size = sizeof(ItemHeader<T>);
    ResourcePool<T>* container = 0;
    // array init
    {
        U64 reserve_arr_byte_size = element_byte_size * reserve_element_size;
        ArenaParams arena_params = {};
        arena_params.reserve_size = Max(reserve_arr_byte_size, KB(4));
        arena_params.commit_size = KB(4);
        arena_params.flags = ArenaFlag_NoChain;
        Arena* arena = arena_alloc(&arena_params);
        container = PushStruct(arena, ResourcePool<T>);
        container->arena = arena;
        container->items = (ItemHeader<T>*)((U8*)arena + arena_pos(arena));
        // zero idx is nil
        PushStruct(container->arena, ItemHeader<T>);
        container->size += 1;
    }

    // free list init
    {
        U64 reserved_free_list_bytes = sizeof(*container->free_list) * reserve_element_size;
        ArenaParams free_list_arena_params = {};
        free_list_arena_params.reserve_size = Max(reserved_free_list_bytes, KB(4));
        free_list_arena_params.commit_size = KB(4);
        free_list_arena_params.flags = ArenaFlag_NoChain;
        container->arena_free_list = arena_alloc(&free_list_arena_params);
        container->free_list = (ResourcePoolHandle*)((U8*)container->arena_free_list + arena_pos(container->arena_free_list));
    }

    return container;
}

template <typename T>
g_internal void
resource_pool_release(ResourcePool<T>* container)
{
    arena_release(container->arena_free_list);
    arena_release(container->arena);
}

template <typename T>
g_internal ItemHeader<T>*
_resource_pool_item_from_idx(ResourcePool<T>* container, U32 idx)
{
    ItemHeader<T>* result = &container->items[0];
    T empty = {};
    Assert(MemoryMatchStruct(&result->data, &empty));
    if (idx != 0 && idx < container->size)
    {
        result = &container->items[idx];
    }
    else
    {
        // TODO: create logging for idx OOB and gen_id too old
    }
    result->in_use = true;
    return result;
}

template <typename T>
g_internal T*
resource_pool_item_from_idx(ResourcePool<T>* container, ResourcePoolHandle item_handle)
{
    ItemHeader<T>* result = &container->items[0];
    if (item_handle.idx < container->size && container->items[item_handle.idx].in_use && container->items[item_handle.idx].gen_id == item_handle.gen_id)
    {
        result = &container->items[item_handle.idx];
    }
    return &result->data;
}

template <typename T>
g_internal ResourcePoolHandle
resource_pool_array_idx_get(ResourcePool<T>* container)
{
    U32 item_idx = 0;
    if (container->free_list_size)
    {
        container->free_list_size -= 1;
        item_idx = container->free_list[container->free_list_size].idx;
    }
    else
    {
        PushStruct(container->arena, ItemHeader<T>);
        item_idx = container->size;
        container->size += 1;
    }

    ItemHeader<T>* item = _resource_pool_item_from_idx(container, item_idx);

    ResourcePoolHandle handle = {};
    handle.idx = item_idx;
    handle.gen_id = item->gen_id;
    return handle;
}

template <typename T>
g_internal void
resource_pool_item_free(ResourcePool<T>* container, ResourcePoolHandle item_handle)
{
    ItemHeader<T>* item = _resource_pool_item_from_idx(container, item_handle.idx);
    if (item_handle.idx > 0 && item->gen_id == item_handle.gen_id)
    {
        container->free_list[container->free_list_size] = item_handle;
        container->free_list_size += 1;
        item->in_use = false;
        item->gen_id += 1;
    }
}
} // namespace



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
    Debug_SetName(arena, "fixed dynamic array arena");
    this->_arena = arena;
    this->_arr = (T*)((U8*)arena + arena_pos(arena));
    this->_capacity = (reserve_arr_byte_size - ARENA_HEADER_SIZE) / element_byte_size;
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
        Debug_SetName(arena, "resource pool arena");
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
        Debug_SetName(container->arena_free_list, "resource pool free list arena");
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

//////////////////////////////////////////////////////////////
// Array Resource Pool
template <typename T>
ArrayResourcePool<T>*
ArrayResourcePool<T>::create(Arena* arena, U32 capacity)
{
    ArrayResourcePool* pool = PushStruct(arena, ArrayResourcePool<T>);
    pool->capacity = Max(capacity, 1);
    pool->items = PushArray(arena, ArrayItemHeader<T>, pool->capacity + 1);
    for (U32 i = 1; i < pool->capacity; i++)
    {
        pool->items[i].next = i + 1;
    }
    pool->free_list = 1;
    return pool;
}

template <typename T>
bool
ArrayResourcePool<T>::item_from_handle(ArrayResourcePoolHandle item_handle, T** out_value)
{
    bool success = false;
    ArrayItemHeader<T>* result = &this->items[0];
    if (item_handle.idx > 0 && item_handle.idx <= this->capacity && this->items[item_handle.idx].in_use && this->items[item_handle.idx].gen_id == item_handle.gen_id)
    {
        result = &this->items[item_handle.idx];
        success = true;
    }
    *out_value = &result->data;
    return success;
}

template <typename T>
ArrayResourcePoolHandle
ArrayResourcePool<T>::handle_get()
{
    AssertAlways(this->free_list != 0);

    U32 idx = this->free_list;

    ArrayItemHeader<T>* item = &this->items[idx];
    this->free_list = item->next;
    item->in_use = true;
    item->next = 0;

    U32 gen_id = this->items[idx].gen_id;
    ArrayResourcePoolHandle handle = {idx, gen_id};
    return handle;
}

template <typename T>
void
ArrayResourcePool<T>::item_free(ArrayResourcePoolHandle item_handle)
{
    U32 item_idx = item_handle.idx;
    if (item_handle.idx > 0 && item_handle.idx <= this->capacity)
    {
        ArrayItemHeader<T>* item = &this->items[item_idx];
        if (item_handle.gen_id == item->gen_id)
        {
            item->next = this->free_list;
            this->free_list = item_idx;
            item->in_use = false;
            item->gen_id += 1;
        }
        else
        {
            // TODO: Log
        }
    }
}

template <typename T>
ArrayResourcePoolIterator<T>
ArrayResourcePool<T>::begin()
{
    ArrayResourcePoolIterator<T> iter = {this, 1};
    iter.skip_unused();
    return iter;
}

template <typename T>
ArrayResourcePoolIterator<T>
ArrayResourcePool<T>::end()
{
    ArrayResourcePoolIterator<T> iter = {this, this->capacity + 1};
    return iter;
}

template <typename T>
T&
ArrayResourcePoolIterator<T>::operator*()
{
    Assert(this->pool);
    Assert(this->idx > 0 && this->idx <= this->pool->capacity);
    Assert(this->pool->items[this->idx].in_use);

    T& result = this->pool->items[this->idx].data;
    return result;
}

template <typename T>
T*
ArrayResourcePoolIterator<T>::operator->()
{
    Assert(this->pool);
    Assert(this->idx > 0 && this->idx <= this->pool->capacity);
    Assert(this->pool->items[this->idx].in_use);

    T* result = &this->pool->items[this->idx].data;
    return result;
}

template <typename T>
ArrayResourcePoolIterator<T>&
ArrayResourcePoolIterator<T>::operator++()
{
    this->idx += 1;
    this->skip_unused();
    return *this;
}

template <typename T>
bool
ArrayResourcePoolIterator<T>::operator!=(ArrayResourcePoolIterator<T> other)
{
    bool result = this->pool != other.pool || this->idx != other.idx;
    return result;
}

template <typename T>
bool
ArrayResourcePoolIterator<T>::operator==(ArrayResourcePoolIterator<T> other)
{
    bool result = this->pool == other.pool && this->idx == other.idx;
    return result;
}

template <typename T>
void
ArrayResourcePoolIterator<T>::skip_unused()
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

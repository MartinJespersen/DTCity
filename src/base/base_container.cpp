
template <typename T>
g_internal Container<T>*
container_init(U64 reserve_element_size)
{
    U64 element_byte_size = sizeof(ItemHeader<T>);
    Container<T>* container = 0;
    // array init
    {
        U64 reserve_arr_byte_size = element_byte_size * reserve_element_size;
        ArenaParams arena_params = {};
        arena_params.reserve_size = Max(reserve_arr_byte_size, KB(4));
        arena_params.commit_size = KB(4);
        arena_params.flags = ArenaFlag_NoChain;
        Arena* arena = arena_alloc(&arena_params);
        container = PushStruct(arena, Container<T>);
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
        container->free_list = (ContainerHandle*)((U8*)container->arena_free_list + arena_pos(container->arena_free_list));
    }

    return container;
}

template <typename T>
g_internal void
container_release(Container<T>* container)
{
    arena_release(container->arena_free_list);
    arena_release(container->arena);
}

template <typename T>
g_internal ItemHeader<T>*
_container_item_from_idx(Container<T>* container, U32 idx)
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
container_item_from_idx(Container<T>* container, ContainerHandle item_handle)
{
    ItemHeader<T>* result = &container->items[0];
    if (item_handle.idx < container->size && container->items[item_handle.idx].in_use && container->items[item_handle.idx].gen_id == item_handle.gen_id)
    {
        result = &container->items[item_handle.idx];
    }
    return &result->data;
}

template <typename T>
g_internal ContainerHandle
container_array_idx_get(Container<T>* container)
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

    ItemHeader<T>* item = _container_item_from_idx(container, item_idx);

    ContainerHandle handle = {};
    handle.idx = item_idx;
    handle.gen_id = item->gen_id;
    return handle;
}

template <typename T>
g_internal void
container_item_free(Container<T>* container, ContainerHandle item_handle)
{
    ItemHeader<T>* item = _container_item_from_idx(container, item_handle.idx);
    if (item_handle.idx > 0 && item->gen_id == item_handle.gen_id)
    {
        container->free_list[container->free_list_size] = item_handle;
        container->free_list_size += 1;
        item->in_use = false;
        item->gen_id += 1;
    }
}

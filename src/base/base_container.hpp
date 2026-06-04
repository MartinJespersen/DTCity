template <typename T>
struct ItemHeader
{
    U32 gen_id;
    T data;
};

template <typename T>
struct Container
{
    Arena* arena; // should only contain data from array after init has been called
    ItemHeader<T>* items;
    U64 size;
    U32 element_byte_size;

    Arena* arena_free_list;
    U64* free_list;
};

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
        arena_params.flags = arena_default_flags;
        arena_params.flags = ArenaFlag_NoChain;
        Arena* arena = arena_alloc(&arena_params);
        container = PushStruct(arena, Container<T>);
        container->arena = arena;
        container->element_byte_size = element_byte_size;
        container->items = (ItemHeader<T>*)((U8*)arena + arena_pos(arena));
        // zero idx is nil
        PushStruct(container->arena, ItemHeader<T>);
    }

    // free list init
    {
        U64 reserved_free_list_bytes = sizeof(*container->free_list) * reserve_element_size;
        ArenaParams free_list_arena_params = {};
        free_list_arena_params.reserve_size = Max(reserved_free_list_bytes, KB(4));
        free_list_arena_params.commit_size = KB(4);
        free_list_arena_params.flags = arena_default_flags;
        free_list_arena_params.flags = ArenaFlag_NoChain;
        container->arena_free_list = arena_alloc(&free_list_arena_params);
        container->free_list = (U64*)((U8*)container->arena_free_list + arena_pos(container->arena_free_list));
    }

    return container;
}

template <typename T>
g_internal U64
container_array_idx_get(Container<T>* container)
{
    U64 elem_idx = container->size;
    PushStruct(container->arena, ItemHeader<T>);

    container->size += 1;
    return elem_idx;
}

template <typename T>
g_internal T*
container_item_from_idx(Container<T>* container, U64 idx)
{
    T* result = &container->items[0].data;
    if (idx < container->size)
    {
        result = &container->items[idx].data;
    }
    return result;
}

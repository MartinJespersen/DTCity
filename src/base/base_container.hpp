
struct ContainerHandle
{
    U32 idx;
    U32 gen_id;
};

template <typename T>
struct ItemHeader
{
    U32 gen_id;
    bool in_use;
    T data;
};

template <typename T>
struct Container
{
    Arena* arena; // should only contain data from array after init has been called
    ItemHeader<T>* items;
    U32 size;

    Arena* arena_free_list;
    ContainerHandle* free_list;
    U32 free_list_size;
};

template <typename T>
g_internal Container<T>*
container_init(U64 reserve_element_size);

template <typename T>
g_internal void
container_release(Container<T>* container);

template <typename T>
g_internal T*
container_item_from_idx(Container<T>* container, ContainerHandle item_handle);

template <typename T>
g_internal ContainerHandle
container_array_idx_get(Container<T>* container);

template <typename T>
g_internal void
container_item_free(Container<T>* container, ContainerHandle item_handle);

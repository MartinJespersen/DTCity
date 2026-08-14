

///////////////////////////////////////////////////////////////////////
// DynamicArenaArray
template <typename T>
struct ArenaArray
{
  private:
    Arena* _arena;
    T* _arr;
    U64 _capacity;

  public:
    U64 size;

    ArenaArray(U64 max_capacity) noexcept;
    ~ArenaArray();

    static void
    release(ArenaArray<T>* arr) noexcept;

    T&
    operator[](U64 index) noexcept
    {
        AssertAlways(index < size);
        return _arr[index];
    }

    T*
    begin() noexcept
    {
        return _arr;
    }

    T*
    end() noexcept
    {
        return _arr + size;
    }

    T*
    push(T& item) noexcept;

    void
    push(T* arr, U64 size) noexcept;
};

/////////////////////////////////////////////////////////////////////////
// Resource Pool /////////////////////////////////////////////////////////
struct ResourcePoolHandle
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
struct ResourcePool
{
    Arena* arena; // should only contain data from array after init has been called
    ItemHeader<T>* items;
    U32 size;

    Arena* arena_free_list;
    ResourcePoolHandle* free_list;
    U32 free_list_size;
};

template <typename T>
g_internal ResourcePool<T>*
resource_pool_init(U64 reserve_element_size);

template <typename T>
g_internal void
resource_pool_release(ResourcePool<T>* container);

template <typename T>
g_internal T*
resource_pool_item_from_idx(ResourcePool<T>* container, ResourcePoolHandle item_handle);

template <typename T>
g_internal ResourcePoolHandle
resource_pool_array_idx_get(ResourcePool<T>* container);

template <typename T>
g_internal void
resource_pool_item_free(ResourcePool<T>* container, ResourcePoolHandle item_handle);

/////////////////////////////////////////////////////////////////////////
// Array Resource Pool /////////////////////////////////////////////////////////

struct ArrayResourcePoolHandle
{
    U32 idx;
    U32 gen_id;

    bool
    operator==(ArrayResourcePoolHandle& other)
    {
        return idx == other.idx && gen_id == other.gen_id;
    }
};

template <typename T>
struct ArrayItemHeader
{
    U32 next;
    U32 gen_id;
    bool in_use;
    T data;
};

template <typename T>
struct ArrayResourcePool;

template <typename T>
struct ArrayResourcePoolIterator
{
    ArrayResourcePool<T>* pool;
    U32 idx;

    T&
    operator*();

    T*
    operator->();

    ArrayResourcePoolIterator<T>&
    operator++();

    bool
    operator!=(ArrayResourcePoolIterator<T> other);

    bool
    operator==(ArrayResourcePoolIterator<T> other);

    void
    skip_unused();
};

template <typename T>
struct ArrayResourcePool
{
    ArrayItemHeader<T>* items;
    U32 capacity;
    U32 free_list;

    static ArrayResourcePool<T>*
    create(Arena* arena, U32 capacity);

    bool
    item_from_handle(ArrayResourcePoolHandle item_handle, T** out_value);

    ArrayResourcePoolHandle
    handle_get();

    void
    item_free(ArrayResourcePoolHandle item_handle);

    ArrayResourcePoolIterator<T>
    begin();

    ArrayResourcePoolIterator<T>
    end();
};

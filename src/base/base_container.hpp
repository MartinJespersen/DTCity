namespace
{
// Dynamic Array ////////////////////////////////////////////////////////
template <typename T>
struct DynamicArray
{
    T* data;
    U64 size;
    U64 capacity;

    DynamicArray() = default;
    DynamicArray(const DynamicArray<T>& other) = delete;
    DynamicArray<T>&
    operator=(const DynamicArray<T>& other) = delete;

    DynamicArray(DynamicArray<T>&& other) noexcept
    {
        data = other.data;
        size = other.size;
        capacity = other.capacity;
        other.data = 0;
        other.size = 0;
        other.capacity = 0;
    }

    DynamicArray<T>&
    operator=(DynamicArray<T>&& other) noexcept
    {
        if (this != &other)
        {
            data = other.data;
            size = other.size;
            capacity = other.capacity;
            other.data = 0;
            other.size = 0;
            other.capacity = 0;
        }
        return *this;
    }
};

struct DynamicArrayNode
{
    DynamicArrayNode* next;
};

constexpr U32 g_dynamic_array_min_idx = 6; // min of 64 bytes
constexpr U32 g_dynamic_array_max_idx = 26;
constexpr U32 g_dynamic_array_max_align = 64;

struct DynamicArrayPool
{
    OS_Handle mutex;
    Arena* arena;
    DynamicArrayNode** free_list;
};

DynamicArrayPool* g_dynamic_array_pool = 0;

lib_internal void
dynamic_array_init(U64 bytes_to_reserve = GB(1));

lib_internal void
dynamic_array_release();

template <typename T>
lib_internal DynamicArray<T>
dynamic_array_create(U64 start_capacity);

template <typename T>
lib_internal void
dynamic_array_destroy(DynamicArray<T>* arr);

template <typename T>
lib_internal void
dynamic_array_append(DynamicArray<T>* arr, Buffer<T> buffer);

template <typename T>
lib_internal void
dynamic_array_from_buffer_overwrite(DynamicArray<T>* arr, Buffer<T> buffer);

lib_internal U64
_dynamic_array_alloc_size_find(U64 elem_count, U64 elem_size, U64 arr_offset);

template <typename T>
lib_internal U64
_dynamic_array_offset_calc();

lib_internal U32
_free_list_idx_from_alloc_size(U64 alloc_size);

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
} // namespace

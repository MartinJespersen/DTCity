
constexpr U32 POW2_FREELIST_ALIGN = 64;
struct Pow2FreelistNode
{
    Pow2FreelistNode* next;
    U64 gen_id;
};
static_assert(sizeof(Pow2FreelistNode) <= POW2_FREELIST_ALIGN, "Pow2FreelistNode is too large");

struct Pow2Freelist
{
    Pow2FreelistNode** free_list;
    U32 exponent_min;
    U32 exponent_count;
};

template <typename T>
struct BufferHandle
{
    Buffer<T> buffer;
    U64 gen_id;

    BufferHandle(T* ptr, U64 size, U64 gen_id)
    {
        this->buffer.data = ptr;
        this->buffer.size = size;
        this->gen_id = gen_id;
    }

    bool
    buffer_try_get(Buffer<T>* out);
};

lib_internal Pow2Freelist*
pow2_freelist_create(Arena* arena, U32 exponent_min = 6, U32 exponent_count = 20);

template <typename T>
lib_internal BufferHandle<T>
buffer_from_pow2_freelist(Arena* arena, Pow2Freelist* pool, U64 size);

template <typename T>
lib_internal bool
buffer_push_to_freelist(Pow2Freelist* freelist, BufferHandle<T>& handle);

template <typename T>
lib_internal BufferHandle<T>
buffer_handle_from_chunk_list(Arena* arena, Pow2Freelist* freelist, ChunkList<T>* list);
lib_internal U64
_pow2_freelist_alloc_size_find(U64 size, U64 arr_offset, U32 exponent_min);

template <typename T>
lib_internal U32
_pow2_freelist_offset_calc();
lib_internal U32
_pow2_freelist_idx_from_alloc_size(U64 alloc_size, U32 exponent_min, U32 exponent_count);

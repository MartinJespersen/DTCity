namespace async
{

template <typename T> struct HeapItem
{
    S64 k;
    T v;
};

template <typename T> struct Heap
{
    OS_Handle mutex;
    U64 size;
    SegmentBuffer<HeapItem<T>> buffer;
};

// min heap declarations
template <typename T>
g_internal Heap<T>*
async_heap_alloc();

template <typename T>
g_internal void
async_heap_release(Heap<T>* heap);

template <typename T>
g_internal void
async_min_heap_push(Heap<T>* heap, const T& v, S64 k);

template <typename T>
g_internal void
async_min_heap_pop(Heap<T>* heap);

template <typename T>
g_internal B32
async_min_heap_pop_ready(Heap<T>* heap, S64 max_key, HeapItem<T>* out_value);

template <typename T>
g_internal B32
async_min_heap_peek(Heap<T>* heap, HeapItem<T>* out_value);

} // namespace async

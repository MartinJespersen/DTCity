namespace async
{

template <typename T>
static void
_heap_index_swap(Heap<T>* heap, U64 a, U64 b)
{
    AssertAlways(a < heap->size);
    AssertAlways(b < heap->size);

    HeapItem<T> temp_a = segment_buffer_item_get(&heap->buffer, a);
    HeapItem<T> temp_b = segment_buffer_item_get(&heap->buffer, b);
    segment_buffer_insert(&heap->buffer, temp_b, a);
    segment_buffer_insert(&heap->buffer, temp_a, b);
}

template <typename T>
static void
_async_min_heap_pop_unlocked(Heap<T>* heap)
{
    AssertAlways(heap->size > 0);

    U64 last_idx = heap->size - 1;
    _heap_index_swap(heap, 0, last_idx);
    MemoryZeroStruct(&segment_buffer_item_get(&heap->buffer, last_idx));
    heap->size = last_idx;

    U64 cur_idx = 0;
    for (U64 c0_idx = 1; c0_idx < heap->size; c0_idx = cur_idx * 2 + 1)
    {
        U64 c1_idx = c0_idx + 1;
        U64 smallest_child_idx = c0_idx;
        HeapItem<T>& c0_item = segment_buffer_item_get(&heap->buffer, c0_idx);

        if (c1_idx < heap->size)
        {
            HeapItem<T>& c1_item = segment_buffer_item_get(&heap->buffer, c1_idx);
            if (c1_item.k < c0_item.k)
            {
                smallest_child_idx = c1_idx;
            }
        }

        HeapItem<T>& cur_item = segment_buffer_item_get(&heap->buffer, cur_idx);
        HeapItem<T>& smallest_child_item = segment_buffer_item_get(&heap->buffer, smallest_child_idx);
        if (cur_item.k <= smallest_child_item.k)
        {
            break;
        }

        _heap_index_swap(heap, cur_idx, smallest_child_idx);
        cur_idx = smallest_child_idx;
    }
}

template <typename T>
g_internal Heap<T>*
async_heap_alloc()
{
    Arena* arena = arena_alloc();
    Heap<T>* heap = PushStruct(arena, Heap<T>);
    heap->mutex = os_rw_mutex_alloc();
    heap->buffer.arena = arena;
    return heap;
}

template <typename T>
g_internal void
async_heap_release(Heap<T>* heap)
{
    if (heap)
    {
        Arena* arena = heap->buffer.arena;
        OS_Handle mutex = heap->mutex;
        os_rw_mutex_release(mutex);
        arena_release(arena);
    }
}

template <typename T>
g_internal void
async_min_heap_push(Heap<T>* heap, const T& v, S64 k)
{
    HeapItem<T> item = {};
    item.k = k;
    item.v = v;

    os_mutex_scope_w(heap->mutex)
    {
        U64 cur_idx = heap->size;
        heap->size += 1;
        segment_buffer_insert(&heap->buffer, item, cur_idx);

        while (cur_idx > 0)
        {
            U64 parent_idx = (cur_idx - 1) / 2;
            HeapItem<T>& cur_item = segment_buffer_item_get(&heap->buffer, cur_idx);
            HeapItem<T>& parent_item = segment_buffer_item_get(&heap->buffer, parent_idx);

            if (cur_item.k >= parent_item.k)
            {
                break;
            }

            _heap_index_swap(heap, parent_idx, cur_idx);
            cur_idx = parent_idx;
        }
    }
}

template <typename T>
g_internal void
async_min_heap_pop(Heap<T>* heap)
{
    os_mutex_scope_w(heap->mutex)
    {
        if (heap->size > 0)
        {
            _async_min_heap_pop_unlocked(heap);
        }
    }
}

template <typename T>
g_internal B32
async_min_heap_pop_ready(Heap<T>* heap, S64 max_key, HeapItem<T>* out_value)
{
    B32 result = false;
    os_mutex_scope_w(heap->mutex)
    {
        if (heap->size > 0)
        {
            HeapItem<T> item = segment_buffer_item_get(&heap->buffer, 0);
            if (item.k <= max_key)
            {
                result = true;
                if (out_value)
                {
                    *out_value = item;
                }
                _async_min_heap_pop_unlocked(heap);
            }
        }
    }
    return result;
}

template <typename T>
g_internal B32
async_min_heap_peek(Heap<T>* heap, HeapItem<T>* out_value)
{
    B32 result = false;
    os_mutex_scope_r(heap->mutex)
    {
        if (heap->size > 0)
        {
            result = true;
            if (out_value)
            {
                *out_value = segment_buffer_item_get(&heap->buffer, 0);
            }
        }
    }
    return result;
}

} // namespace async

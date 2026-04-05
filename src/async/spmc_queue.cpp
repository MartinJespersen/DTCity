namespace async
{

template <typename T>
static U64
_spmc_queue_capacity(SpmcQueue<T>* queue)
{
    return segment_buffer_capacity(&queue->buffer);
}

template <typename T>
SpmcQueue<T>*
spmc_queue_create(Arena* arena, U64 capacity)
{
    Assert(capacity > 0);

    SpmcQueue<T>* queue = PushStruct(arena, SpmcQueue<T>);
    queue->arena = arena;
    queue->top.store(0, std::memory_order_relaxed);
    queue->bottom.store(0, std::memory_order_relaxed);
    queue->buffer.segment_count.store(0, std::memory_order_relaxed);
    for (U64 segment_index = 0; segment_index < SPMC_QUEUE_MAX_SEGMENT_COUNT; segment_index++)
    {
        queue->buffer.segments[segment_index].store(0, std::memory_order_relaxed);
    }
    _spmc_queue_reserve(queue, capacity);
    return queue;
}

template <typename T>
void
spmc_queue_destroy(SpmcQueue<T>* queue)
{
    if (queue)
    {
        arena_release(queue->arena);
    }
}

template <typename T>
B32
spmc_queue_push(SpmcQueue<T>* queue, const T& value)
{
    U64 bottom = queue->bottom.load(std::memory_order_relaxed);
    U64 segment_index = segment_buffer_segment_index<T>(bottom);
    _spmc_queue_buffer_ensure_segment(queue, segment_index);
    spmc_queue_slot_get(&queue->buffer, bottom) = value;
    std::atomic_thread_fence(std::memory_order_release);
    queue->bottom.store(bottom + 1, std::memory_order_relaxed);
    return true;
}

template <typename T>
B32
spmc_queue_pop(SpmcQueue<T>* queue, T& value)
{
    U64 bottom = queue->bottom.load(std::memory_order_relaxed);
    if (bottom == 0)
    {
        return false;
    }

    bottom -= 1;
    queue->bottom.store(bottom, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_seq_cst);

    U64 top = queue->top.load(std::memory_order_relaxed);
    if (top > bottom)
    {
        queue->bottom.store(bottom + 1, std::memory_order_relaxed);
        return false;
    }

    T popped_value = spmc_queue_slot_get(&queue->buffer, bottom);

    if (top == bottom)
    {
        U64 expected_top = top;
        if (!queue->top.compare_exchange_strong(expected_top, top + 1, std::memory_order_seq_cst, std::memory_order_relaxed))
        {
            queue->bottom.store(bottom + 1, std::memory_order_relaxed);
            return false;
        }

        queue->bottom.store(bottom + 1, std::memory_order_relaxed);
    }

    value = popped_value;
    return true;
}

template <typename T>
B32
spmc_queue_steal(SpmcQueue<T>* queue, T& value)
{
    U64 top = queue->top.load(std::memory_order_acquire);
    std::atomic_thread_fence(std::memory_order_seq_cst);
    U64 bottom = queue->bottom.load(std::memory_order_acquire);
    if (top >= bottom)
    {
        return false;
    }

    T stolen_value = spmc_queue_slot_get<T>(&queue->buffer, top);
    if (!queue->top.compare_exchange_strong(top, top + 1, std::memory_order_seq_cst, std::memory_order_relaxed))
    {
        return false;
    }

    value = stolen_value;
    return true;
}

template <typename T>
static void
_spmc_queue_buffer_ensure_segment(SpmcQueue<T>* queue, U64 segment_index)
{
    Assert(segment_index < SPMC_QUEUE_MAX_SEGMENT_COUNT);

    U64 segment_count = queue->buffer.segment_count.load(std::memory_order_acquire);
    while (segment_count <= segment_index)
    {
        U64 next_segment = segment_count;
        T* segment = PushArray(queue->arena, T, segment_buffer_capacity<T>(next_segment));
        queue->buffer.segments[next_segment].store(segment, std::memory_order_release);
        segment_count = next_segment + 1;
        queue->buffer.segment_count.store(segment_count, std::memory_order_release);
    }
}

template <typename T>
static void
_spmc_queue_reserve(SpmcQueue<T>* queue, U64 capacity)
{
    U64 segment_count = 1;
    while (segment_buffer_capacity_from_segment_count<T>(segment_count) < capacity)
    {
        segment_count++;
        Assert(segment_count <= SPMC_QUEUE_MAX_SEGMENT_COUNT);
    }

    _spmc_queue_buffer_ensure_segment(queue, segment_count - 1);
}

template <typename T>
static U64
spmc_queue_capacity(AsyncSpmcFifoQueue<T>* buffer)
{
    U64 segment_count = buffer->segment_count.load(std::memory_order_acquire);
    return segment_buffer_capacity_from_segment_count<T>(segment_count);
}

template <typename T>
static T&
spmc_queue_slot_get(AsyncSpmcFifoQueue<T>* buffer, U64 index)
{
    U64 segment_index = segment_buffer_segment_index<T>(index);
    U64 segment_offset = index - segment_buffer_capacity_from_segment_count<T>(segment_index);
    T* segment = buffer->segments[segment_index].load(std::memory_order_acquire);
    Assert(segment);
    return segment[segment_offset];
}
} // namespace async

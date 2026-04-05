namespace async
{

template <typename T>
static U64
segment_buffer_capacity(U64 segment_index)
{
    Assert(segment_index < SEGMENT_BUFFER_MAX_SEGMENT_COUNT);
    return 1ull << (SEGMENT_BUFFER_SHIFT + segment_index);
}

template <typename T>
static U64
segment_buffer_capacity_from_segment_count(U64 segment_count)
{
    Assert(segment_count <= SEGMENT_BUFFER_MAX_SEGMENT_COUNT);

    if (segment_count == 0)
    {
        return 0;
    }

    return (1ull << (SEGMENT_BUFFER_SHIFT + segment_count)) - (1ull << SEGMENT_BUFFER_SHIFT);
}

template <typename T>
static U64
segment_buffer_segment_index(U64 index)
{
    U64 shifted_index = (index >> SEGMENT_BUFFER_SHIFT) + 1;
    return 63 - clz64(shifted_index);
}

template <typename T>
g_internal void
segment_buffer_ensure_size(SegmentBuffer<T>* segment_buffer, U64 segment_index)
{
    Assert(segment_index < SEGMENT_BUFFER_MAX_SEGMENT_COUNT);
    Assert(segment_buffer->arena);

    U64 segment_count = segment_buffer->segment_count;
    while (segment_count <= segment_index)
    {
        U64 next_segment = segment_count;
        T* segment = PushArray(segment_buffer->arena, T, segment_buffer_capacity<T>(next_segment));
        segment_buffer->segments[next_segment] = segment;
        segment_count = next_segment + 1;
        segment_buffer->segment_count = segment_count;
    }
}

template <typename T>
g_internal T&
segment_buffer_item_get(SegmentBuffer<T>* buffer, U64 index)
{
    U64 segment_index = segment_buffer_segment_index<T>(index);
    segment_buffer_ensure_size(buffer, segment_index);
    U64 segment_offset = index - segment_buffer_capacity_from_segment_count<T>(segment_index);
    return buffer->segments[segment_index][segment_offset];
}

template <typename T>
g_internal void
segment_buffer_insert(SegmentBuffer<T>* buffer, const T& v, U64 index)
{
    T& item = segment_buffer_item_get(buffer, index);
    item = v;
}
} // namespace async

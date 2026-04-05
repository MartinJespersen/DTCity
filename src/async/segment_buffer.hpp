namespace async
{

static constexpr U64 SEGMENT_BUFFER_SHIFT = 6;
static constexpr U64 SEGMENT_BUFFER_MAX_SEGMENT_COUNT = 26;
template <typename T> struct SegmentBuffer
{
    Arena* arena;
    U64 segment_count;
    T* segments[SEGMENT_BUFFER_MAX_SEGMENT_COUNT];
};

template <typename T>
static U64
segment_buffer_capacity(U64 segment_index);

template <typename T>
static U64
segment_buffer_capacity_from_segment_count(U64 segment_count);

template <typename T>
static U64
segment_buffer_segment_index(U64 index);

template <typename T>
g_internal void
segment_buffer_ensure_size(SegmentBuffer<T>* segment_buffer, U64 segment_index);

template <typename T>
g_internal T&
segment_buffer_item_get(SegmentBuffer<T>* buffer, U64 index);

template <typename T>
g_internal void
segment_buffer_insert(SegmentBuffer<T>* buffer, const T& v, U64 index);

} // namespace async

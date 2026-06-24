#pragma once

#include <atomic>

namespace async
{

static constexpr U64 SPMC_QUEUE_SEGMENT_SHIFT = 6;
static constexpr U64 SPMC_QUEUE_MAX_SEGMENT_COUNT = 26;
template <typename T> struct AsyncSpmcFifoQueue
{
    std::atomic<U64> segment_count;
    std::atomic<T*> segments[SPMC_QUEUE_MAX_SEGMENT_COUNT];
};

template <typename T> struct SpmcQueue
{
    Arena* arena;
    std::atomic<U64> top;
    std::atomic<U64> bottom;
    AsyncSpmcFifoQueue<T> buffer;
};

template <typename T>
static U64
_spmc_queue_capacity(SpmcQueue<T>* queue);

template <typename T>
SpmcQueue<T>*
spmc_queue_create(Arena* arena, U64 capacity);

template <typename T>
void
spmc_queue_destroy(SpmcQueue<T>* queue);

template <typename T>
B32
spmc_queue_push(SpmcQueue<T>* queue, const T& value);

template <typename T>
B32
spmc_queue_pop(SpmcQueue<T>* queue, T& value);

template <typename T>
B32
spmc_queue_steal(SpmcQueue<T>* queue, T& value);

template <typename T>
static U64
spmc_queue_capacity(AsyncSpmcFifoQueue<T>* buffer);

template <typename T>
static T&
spmc_queue_slot_get(AsyncSpmcFifoQueue<T>* buffer, U64 index);

} // namespace async

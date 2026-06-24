#pragma once
namespace async
{
template <typename T>
struct Queue
{
    volatile U32 next_index;
    volatile U32 fill_index;
    U32 items_in_queue_count;
    U32 queue_size;
    Arena* arena;
    T* items;
    OS_Handle mutex;
};

////////////////////////////////////////////////
template <typename T>
static void
_queue_grow(Queue<T>* queue, U32 min_queue_size);
template <typename T>
static void
_queue_insert_value(Queue<T>* queue, T* data);
template <typename T>
static Queue<T>*
queue_alloc(Arena* arena, U32 queue_size);
template <typename T>
static void
queue_release(Queue<T>* queue);
template <typename T>
static B32
queue_try_read(Queue<T>* queue, T* item);
template <typename T>
static B32
queue_try_push(Queue<T>* queue, T* data);
template <typename T>
static void
queue_push(Queue<T>* queue, T* data);
} // namespace async

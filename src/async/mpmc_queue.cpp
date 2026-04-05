
namespace async
{

template <typename T>
g_internal void
_insert_value(Queue<T>* queue, T* data)
{
    U32 fill_index = queue->fill_index;
    queue->fill_index = (fill_index + 1) % queue->queue_size;
    queue->items[fill_index] = *data;
    queue->items_in_queue_count++;
}

template <typename T>
static Queue<T>*
queue_alloc(Arena* arena, U32 queue_size)
{
    //~mgj: Semaphore size count in the full state is 1 less than the queue size so that assert
    // works in thread worker when the queue is full and full_index is equal to next_index
    Queue<T>* queue = PushStruct(arena, Queue<T>);
    queue->queue_size = queue_size;
    queue->items = PushArray(arena, T, queue_size);
    queue->mutex = os_rw_mutex_alloc();
    return queue;
}

template <typename T>
static void
queue_destroy(Queue<T>* queue)
{
    OS_MutexRelease(queue->mutex);
}

template <typename T>
static void
queue_push(Queue<T>* queue, T* data)
{
    T* item;
    U32 fill_index;
    OS_MutexScopeW(queue->mutex)
    {
        fill_index = queue->fill_index;
        queue->fill_index = (fill_index + 1) % queue->queue_size;
        queue->items[fill_index] = *data;
        Assert(queue->fill_index != queue->next_index);
    }
}

template <typename T>
static B32
queue_try_push(Queue<T>* queue, T* data)
{
    B32 inserted = false;
    OS_MutexScopeW(queue->mutex)
    {
        if (queue->items_in_queue_count < queue->queue_size)
        {
            _insert_value(queue, data);
        }
    }
    return inserted;
}

template <typename T>
static B32
queue_try_read(Queue<T>* queue, T* item)
{
    U32 cur_index;
    B32 has_read = 0;
    OS_MutexScopeW(queue->mutex)
    {
        cur_index = queue->next_index;
        if (cur_index != queue->fill_index)
        {
            queue->next_index = (cur_index + 1) % queue->queue_size;
            *item = queue->items[cur_index];
            has_read = 1;
        }
    }
    return has_read;
}
} // namespace async

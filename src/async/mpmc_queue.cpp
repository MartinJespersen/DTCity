
namespace async
{

template <typename T>
static void
_queue_grow(Queue<T>* queue, U32 min_queue_size)
{
    AssertAlways(queue);

    U32 new_queue_size = ClampBot(queue->queue_size * 2, min_queue_size);
    T* new_items = PushArray(queue->arena, T, new_queue_size);

    for (U32 item_index = 0; item_index < queue->items_in_queue_count; item_index++)
    {
        U32 source_index = (queue->next_index + item_index) % queue->queue_size;
        new_items[item_index] = queue->items[source_index];
    }

    queue->items = new_items;
    queue->queue_size = new_queue_size;
    queue->next_index = 0;
    queue->fill_index = queue->items_in_queue_count;
}

template <typename T>
static void
_queue_insert_value(Queue<T>* queue, T* data)
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
    Queue<T>* queue = PushStruct(arena, Queue<T>);
    queue->queue_size = ClampBot(queue_size, 1);
    queue->arena = arena;
    queue->items = PushArray(arena, T, queue->queue_size);
    queue->mutex = os_rw_mutex_alloc();
    return queue;
}

template <typename T>
static void
queue_release(Queue<T>* queue)
{
    os_rw_mutex_release(queue->mutex);
}

template <typename T>
static void
queue_push(Queue<T>* queue, T* data)
{
    os_mutex_scope_w(queue->mutex)
    {
        if (queue->items_in_queue_count >= queue->queue_size)
        {
            _queue_grow(queue, queue->queue_size + 1);
        }

        _queue_insert_value(queue, data);
    }
}

template <typename T>
static B32
queue_try_push(Queue<T>* queue, T* data)
{
    B32 inserted = false;
    os_mutex_scope_w(queue->mutex)
    {
        if (queue->items_in_queue_count < queue->queue_size)
        {
            _queue_insert_value(queue, data);
            inserted = true;
        }
    }
    return inserted;
}

template <typename T>
static B32
queue_try_read(Queue<T>* queue, T* item)
{
    B32 has_read = 0;
    os_mutex_scope_w(queue->mutex)
    {
        if (queue->items_in_queue_count > 0)
        {
            U32 cur_index = queue->next_index;
            queue->next_index = (cur_index + 1) % queue->queue_size;
            *item = queue->items[cur_index];
            queue->items_in_queue_count--;
            has_read = 1;
        }
    }
    return has_read;
}
} // namespace async

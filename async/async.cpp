namespace async
{
Queue*
QueueInit(Arena* arena, U32 queue_size, U32 thread_count)
{
    //~mgj: Semaphore size count in the full state is 1 less than the queue size so that assert
    // works in thread worker when the queue is full and full_index is equal to next_index
    U32 semaphore_full_size = queue_size - 1;
    Queue* queue = PushStruct(arena, Queue);
    queue->queue_size = queue_size;
    queue->items = PushArray(arena, QueueItem, queue_size);
    queue->mutex = OS_RWMutexAlloc();
    queue->semaphore_empty =
        OS_SemaphoreAlloc(0, thread_count, Str8CString("queue_empty_semaphore"));
    queue->semaphore_full = OS_SemaphoreAlloc(semaphore_full_size, semaphore_full_size,
                                              Str8CString("queue_full_semaphore"));
    return queue;
}

void
QueueDestroy(Queue* queue)
{
    OS_MutexRelease(queue->mutex);
    OS_SemaphoreRelease(queue->semaphore_empty);
}

void
QueuePush(Queue* queue, void* data, WorkerFunc worker_func)
{
    QueueItem* item;
    U32 fill_index;
    OS_SemaphoreTake(queue->semaphore_full, max_U64);
    OS_MutexScopeW(queue->mutex)
    {
        fill_index = queue->fill_index;
        queue->fill_index = (fill_index + 1) % queue->queue_size;
        Assert(queue->fill_index != queue->next_index);
    }
    OS_SemaphoreDrop(queue->semaphore_empty);
    item = &queue->items[fill_index];
    item->data = data;
    item->worker_func = worker_func;
}

void
ThreadWorker(void* data)
{
    ThreadInput* input = (ThreadInput*)data;
    Queue* queue = input->queue;
    U32 thread_count = input->thread_count;
    U32 thread_id = input->thread_id;

    ThreadInfo thread_info;
    thread_info.thread_id = thread_id;
    thread_info.queue = queue;

    QueueItem item;
    U32 cur_index;
    B32 is_waiting = 0;
    while (true)
    {
        OS_MutexScopeW(queue->mutex)
        {
            cur_index = queue->next_index;
            if (cur_index != queue->fill_index)
            {
                queue->next_index = (cur_index + 1) % queue->queue_size;
            }
            else
            {
                is_waiting = 1;
            }
        }
        if (is_waiting)
        {
            OS_SemaphoreTake(queue->semaphore_empty, max_U64);
            is_waiting = 0;
        }
        else
        {
            OS_SemaphoreDrop(queue->semaphore_full);
            QueueItem* item = &queue->items[cur_index];
            item->worker_func(thread_info, item->data);
        }
    }
}
} // namespace async

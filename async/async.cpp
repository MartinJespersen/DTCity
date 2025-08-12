namespace async
{
template <typename T>
static Queue<T>*
QueueInit(Arena* arena, U32 queue_size, U32 thread_count)
{
    //~mgj: Semaphore size count in the full state is 1 less than the queue size so that assert
    // works in thread worker when the queue is full and full_index is equal to next_index
    U32 semaphore_full_size = queue_size - 1;
    Queue<T>* queue = PushStruct(arena, Queue<T>);
    queue->queue_size = queue_size;
    queue->items = PushArray(arena, T, queue_size);
    queue->mutex = OS_RWMutexAlloc();
    queue->semaphore_empty =
        OS_SemaphoreAlloc(0, thread_count, Str8CString("queue_empty_semaphore"));
    queue->semaphore_full = OS_SemaphoreAlloc(semaphore_full_size, semaphore_full_size,
                                              Str8CString("queue_full_semaphore"));
    return queue;
}

template <typename T>
static void
QueueDestroy(Queue<T>* queue)
{
    OS_MutexRelease(queue->mutex);
    OS_SemaphoreRelease(queue->semaphore_empty);
}

template <typename T>
static void
QueuePush(Queue<T>* queue, T* data)
{
    T* item;
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
    *item = *data;
}

template <typename T>
static B32
QueueTryRead(Queue<T>* queue, T* item)
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
            OS_SemaphoreDrop(queue->semaphore_full);
            has_read = 1;
        }
    }
    return has_read;
}

static void
ThreadWorker(void* data)
{
    ThreadInput* input = (ThreadInput*)data;
    Queue<QueueItem>* queue = input->queue;
    U32 thread_count = input->thread_count;
    U32 thread_id = input->thread_id;

    ThreadInfo thread_info;
    thread_info.thread_id = thread_id;
    thread_info.queue = queue;

    QueueItem item;
    U32 cur_index;
    B32 is_waiting = 0;
    while (!(*input->kill_switch))
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
            QueueItem item = queue->items[cur_index];
            item.worker_func(thread_info, item.data);
        }
    }
}
static Threads*
WorkerThreadsCreate(Arena* arena, U32 thread_count, U32 queue_size)
{
    Queue<QueueItem>* queue = QueueInit<QueueItem>(arena, queue_size, thread_count);
    Threads* thread_info = PushStruct(arena, Threads);
    thread_info->thread_handles = BufferAlloc<OS_Handle>(arena, thread_count);
    thread_info->msg_queue = queue;
    thread_info->kill_switch = 0;

    for (U32 i = 0; i < thread_count; i++)
    {
        ThreadInput* input = PushStruct(arena, ThreadInput);
        input->queue = queue;
        input->thread_count = thread_count;
        input->thread_id = i;
        input->kill_switch = &thread_info->kill_switch;
        thread_info->thread_handles.data[i] = OS_ThreadLaunch(ThreadWorker, input, NULL);
    }

    return thread_info;
}

static void
WorkerThreadDestroy(Threads* thread_info)
{
    thread_info->kill_switch = 1;
    for (U32 i = 0; i < thread_info->thread_handles.size; i++)

        OS_ThreadJoin(thread_info->thread_handles.data[i], max_U32);
}
} // namespace async

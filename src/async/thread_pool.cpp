namespace async
{
static B32
thread_pool_is_worker_thread(Threads* thread_pool)
{
    return t_thread_pool == thread_pool && t_cur_thread_id != max_U32;
}

static void
thread_pool_signal_work(Threads* thread_pool)
{
    OS_SemaphoreDrop(thread_pool->work_semaphore);
}

static B32
thread_pool_has_registered_external_queue(Threads* thread_pool)
{
    return t_external_queue_pool == thread_pool && t_external_queue_id != max_U32;
}

static U32
thread_pool_external_queue_id(Threads* thread_pool)
{
    if (!thread_pool_has_registered_external_queue(thread_pool))
    {
        U32 queue_id = thread_pool->next_external_queue_index.fetch_add(1);
        AssertAlways(queue_id < thread_pool->external_queue_capacity);
        t_external_queue_pool = thread_pool;
        t_external_queue_id = queue_id;
    }

    return t_external_queue_id;
}

static B32
thread_pool_try_claim_local(Threads* thread_pool, U32 thread_id, QueueItem* item)
{
    return spmc_queue_pop(thread_pool->worker_queues.data[thread_id], *item);
}

static B32
thread_pool_try_claim_external(Threads* thread_pool, QueueItem* item)
{
    U32 queue_count = Min(thread_pool->next_external_queue_index.load(), thread_pool->external_queue_capacity);
    for (U32 queue_id = 0; queue_id < queue_count; queue_id++)
    {
        if (spmc_queue_steal(thread_pool->external_queues.data[queue_id], *item))
        {
            return true;
        }
    }

    return false;
}

static B32
thread_pool_try_steal(Threads* thread_pool, U32 thread_id, QueueItem* item)
{
    if (thread_pool->thread_count <= 1)
    {
        return false;
    }

    for (U32 offset = 1; offset < thread_pool->thread_count; offset++)
    {
        U32 victim_id = (thread_id + offset) % thread_pool->thread_count;
        if (spmc_queue_steal(thread_pool->worker_queues.data[victim_id], *item))
        {
            return true;
        }
    }

    return false;
}

static B32
thread_pool_try_get_work(Threads* thread_pool, U32 thread_id, QueueItem* item)
{
    B32 got_work = false;

    if (thread_pool_try_claim_local(thread_pool, thread_id, item))
    {
        got_work = true;
    }
    else if (thread_pool_try_claim_external(thread_pool, item))
    {
        got_work = true;
    }
    else if (thread_pool_try_steal(thread_pool, thread_id, item))
    {
        got_work = true;
    }

    if (got_work)
    {
        thread_pool->pending_task_count.fetch_sub(1);
    }

    return got_work;
}

static B32
thread_pool_try_push(Threads* thread_pool, QueueItem* item)
{
    AssertAlways(thread_pool);
    AssertAlways(item);

    if (thread_pool->thread_count == 0)
    {
        return false;
    }

    if (thread_pool_is_worker_thread(thread_pool))
    {
        if (spmc_queue_push(thread_pool->worker_queues.data[t_cur_thread_id], *item))
        {
            thread_pool->pending_task_count.fetch_add(1);
            thread_pool_signal_work(thread_pool);
            return true;
        }
    }

    if (thread_pool->external_queue_capacity == 0)
    {
        return false;
    }

    U32 external_queue_id = thread_pool_external_queue_id(thread_pool);
    B32 pushed = spmc_queue_push(thread_pool->external_queues.data[external_queue_id], *item);
    if (pushed)
    {
        thread_pool->pending_task_count.fetch_add(1);
        thread_pool_signal_work(thread_pool);
    }

    return pushed;
}

static void
thread_pool_push(Threads* thread_pool, QueueItem* item)
{
    AssertAlways(thread_pool);
    AssertAlways(item);

    while (!thread_pool_try_push(thread_pool, item))
    {
        os_sleep_milliseconds(0);
    }
}

static B32
thread_pool_has_pending_work(Threads* thread_pool)
{
    AssertAlways(thread_pool);
    return thread_pool->pending_task_count.load() > 0 || thread_pool->in_flight_count.load() > 0;
}

static void
ThreadWorker(void* data)
{
    ScratchScope scratch = ScratchScope(0, 0);
    ThreadInput* input = (ThreadInput*)data;
    Threads* thread_pool = input->thread_pool;
    U32 thread_id = input->thread_id;

    os_set_thread_name(PushStr8F(scratch.arena, "ThreadWorker: %zu", thread_id));
    ThreadInfo thread_info;
    t_thread_pool = thread_pool;
    t_cur_thread_id = thread_id;
    thread_info.thread_pool = thread_pool;
    thread_info.thread_id = thread_id;

    QueueItem item = {};
    while (!thread_pool->kill_switch)
    {
        if (!thread_pool_try_get_work(thread_pool, thread_id, &item))
        {
            if (thread_pool->kill_switch)
            {
                break;
            }

            OS_SemaphoreTake(thread_pool->work_semaphore, max_U64);
            continue;
        }

        thread_pool->in_flight_count.fetch_add(1);
        item.worker_func(thread_info, item.data);
        thread_pool->in_flight_count.fetch_sub(1);
    }

    t_cur_thread_id = max_U32;
    t_thread_pool = 0;
}

static Threads*
WorkerThreadsCreate(Arena* arena, U32 thread_count, U32 queue_size)
{
    Threads* thread_info = PushStruct(arena, Threads);
    U32 external_queue_capacity = Max(thread_count + 2, 4);
    thread_info->thread_handles = BufferAlloc<OS_Handle>(arena, thread_count);
    thread_info->worker_queues = BufferAlloc<SpmcQueue<QueueItem>*>(arena, thread_count);
    thread_info->external_queues = BufferAlloc<SpmcQueue<QueueItem>*>(arena, external_queue_capacity);
    thread_info->thread_count = thread_count;
    thread_info->external_queue_capacity = external_queue_capacity;
    thread_info->kill_switch = 0;
    thread_info->in_flight_count.store(0);
    thread_info->pending_task_count.store(0);
    thread_info->next_external_queue_index.store(0);
    thread_info->work_semaphore = OS_SemaphoreAlloc(0, max_U32, str8_c_string("thread_pool_work"));

    for (U32 i = 0; i < thread_count; i++)
    {
        Arena* queue_arena = arena_alloc();
        thread_info->worker_queues.data[i] = spmc_queue_create<QueueItem>(queue_arena, queue_size);
    }

    for (U32 i = 0; i < external_queue_capacity; i++)
    {
        Arena* queue_arena = arena_alloc();
        thread_info->external_queues.data[i] = spmc_queue_create<QueueItem>(queue_arena, queue_size);
    }

    for (U32 i = 0; i < thread_count; i++)
    {
        ThreadInput* input = PushStruct(arena, ThreadInput);
        input->thread_pool = thread_info;
        input->thread_id = i;
        thread_info->thread_handles.data[i] = OS_ThreadLaunch(ThreadWorker, input, NULL);
    }

    return thread_info;
}

static void
WorkerThreadsDestroy(Threads* thread_info)
{
    thread_info->kill_switch = 1;
    for (U32 i = 0; i < thread_info->thread_count; i++)
    {
        OS_SemaphoreDrop(thread_info->work_semaphore);
    }

    for (U32 i = 0; i < thread_info->thread_handles.size; i++)
    {
        OS_ThreadJoin(thread_info->thread_handles.data[i], max_U32);
    }

    for (U32 i = 0; i < thread_info->worker_queues.size; i++)
    {
        spmc_queue_destroy(thread_info->worker_queues.data[i]);
    }

    for (U32 i = 0; i < thread_info->external_queues.size; i++)
    {
        spmc_queue_destroy(thread_info->external_queues.data[i]);
    }

    OS_SemaphoreRelease(thread_info->work_semaphore);
}
} // namespace async

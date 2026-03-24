namespace async
{
static B32
thread_pool_is_worker_thread(Threads* thread_pool)
{
    return t_thread_pool == thread_pool && t_cur_thread_id != max_U32;
}

static B32
thread_pool_is_registered_thread(Threads* thread_pool)
{
    return t_thread_pool == thread_pool && t_cur_queue_id != max_U32;
}

static U32
thread_pool_submitter_queue_id(Threads* thread_pool)
{
    AssertAlways(thread_pool);
    AssertAlways(thread_pool->worker_queues.size > 0);
    return safe_cast_u32(thread_pool->worker_queues.size - 1);
}

static void
thread_pool_signal_work(Threads* thread_pool)
{
    OS_SemaphoreDrop(thread_pool->work_semaphore);
}

static B32
thread_pool_register_current_thread(Threads* thread_pool)
{
    AssertAlways(thread_pool);

    if (thread_pool_is_worker_thread(thread_pool))
    {
        t_cur_queue_id = t_cur_thread_id;
        return true;
    }

    if (thread_pool_is_registered_thread(thread_pool))
    {
        return true;
    }

    U32 thread_id = os_tid();
    U32 expected_thread_id = 0;
    if (!thread_pool->submitter_thread_os_id.compare_exchange_strong(expected_thread_id, thread_id) && expected_thread_id != thread_id)
    {
        return false;
    }

    t_thread_pool = thread_pool;
    t_cur_thread_id = max_U32;
    t_cur_queue_id = thread_pool_submitter_queue_id(thread_pool);
    return true;
}

static B32
thread_pool_try_claim_local(Threads* thread_pool, U32 thread_id, QueueItem* item)
{
    return spmc_queue_pop(thread_pool->worker_queues.data[thread_id], *item);
}

static B32
thread_pool_try_steal(Threads* thread_pool, U32 thread_id, QueueItem* item)
{
    U32 queue_count = safe_cast_u32(thread_pool->worker_queues.size);
    if (queue_count <= 1)
    {
        return false;
    }

    for (U32 offset = 1; offset < queue_count; offset++)
    {
        U32 victim_id = (thread_id + offset) % queue_count;
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

    if (thread_pool_try_claim_local(thread_pool, thread_id, item) || thread_pool_try_steal(thread_pool, thread_id, item))
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

    if (!thread_pool_register_current_thread(thread_pool))
    {
        return false;
    }

    AssertAlways(t_cur_queue_id < thread_pool->worker_queues.size);
    if (spmc_queue_push(thread_pool->worker_queues.data[t_cur_queue_id], *item))
    {
        thread_pool->pending_task_count.fetch_add(1);
        thread_pool_signal_work(thread_pool);
        return true;
    }

    return false;
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
    t_cur_queue_id = thread_id;
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
    t_cur_queue_id = max_U32;
    t_thread_pool = 0;
}

static Threads*
WorkerThreadsCreate(Arena* arena, U32 thread_count, U32 queue_size)
{
    Threads* thread_info = PushStruct(arena, Threads);
    thread_info->thread_handles = BufferAlloc<OS_Handle>(arena, thread_count);
    thread_info->worker_queues = BufferAlloc<SpmcQueue<QueueItem>*>(arena, thread_count + 1);
    thread_info->thread_count = thread_count;
    thread_info->kill_switch = 0;
    thread_info->in_flight_count.store(0);
    thread_info->pending_task_count.store(0);
    thread_info->submitter_thread_os_id.store(0);
    thread_info->work_semaphore = OS_SemaphoreAlloc(0, max_U32, str8_c_string("thread_pool_work"));

    for (U32 i = 0; i < thread_info->worker_queues.size; i++)
    {
        Arena* queue_arena = arena_alloc();
        thread_info->worker_queues.data[i] = spmc_queue_create<QueueItem>(queue_arena, queue_size);
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

    OS_SemaphoreRelease(thread_info->work_semaphore);
}
} // namespace async

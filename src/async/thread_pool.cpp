namespace async
{

static U64
_thread_pool_next_deadline(ThreadPool* thread_pool)
{
    U64 result = max_U64;

    HeapItem<WorkerTask> heap_item = {};
    if (async::async_min_heap_peek(thread_pool->timer_min_heap, &heap_item))
    {
        result = heap_item.k > 0 ? (U64)heap_item.k : 0;
    }

    return result;
}

static B32
_thread_pool_try_get_work(ThreadPool* thread_pool, WorkerTask* item)
{
    AssertAlways(thread_pool);
    AssertAlways(item);

    if (queue_try_read(thread_pool->mpmc_queue, item))
    {
        thread_pool->pending_task_count.fetch_sub(1);
        return true;
    }

    HeapItem<WorkerTask> heap_item = {};
    U64 now = os_now_microseconds();
    S64 now_s64 = now > (U64)max_S64 ? max_S64 : (S64)now;
    if (async::async_min_heap_pop_ready(thread_pool->timer_min_heap, now_s64, &heap_item))
    {
        *item = heap_item.v;
        thread_pool->pending_task_count.fetch_sub(1);
        return true;
    }

    return false;
}

static void
_thread_pool_wake_workers(ThreadPool* thread_pool, B32 wake_all)
{
    OS_MutexScope(thread_pool->work_mutex)
    {
        thread_pool->work_generation.fetch_add(1);
        if (wake_all)
        {
            os_condition_variable_broadcast(thread_pool->work_cv);
        }
        else
        {
            os_condition_variable_signal(thread_pool->work_cv);
        }
    }
}

static B32
thread_pool_register_current_thread(ThreadPool* thread_pool)
{
    AssertAlways(thread_pool);

    if (t_thread_pool == thread_pool)
    {
        return true;
    }

    t_thread_pool = thread_pool;
    t_cur_thread_id = max_U32;
    return true;
}

static B32
thread_pool_push(ThreadPool* thread_pool, WorkerTask* item, S64 microseconds_delay)
{
    AssertAlways(thread_pool);
    AssertAlways(item);

    if (thread_pool->thread_count == 0 || thread_pool->kill_switch)
    {
        return false;
    }

    if (!thread_pool_register_current_thread(thread_pool))
    {
        return false;
    }

    thread_pool->pending_task_count.fetch_add(1);

    if (microseconds_delay > 0)
    {
        U64 now = os_now_microseconds();
        S64 now_s64 = now > (U64)max_S64 ? max_S64 : (S64)now;
        S64 cutoff_time = now_s64 + microseconds_delay;
        async::async_min_heap_push(thread_pool->timer_min_heap, *item, cutoff_time);
    }
    else
    {
        queue_push(thread_pool->mpmc_queue, item);
    }

    _thread_pool_wake_workers(thread_pool, false);
    return true;
}

static B32
thread_pool_has_pending_work(ThreadPool* thread_pool)
{
    AssertAlways(thread_pool);
    return thread_pool->pending_task_count.load() > 0 || thread_pool->in_flight_count.load() > 0;
}

static void
thread_worker(void* data)
{
    ScratchScope scratch = ScratchScope(0, 0);
    ThreadInput* input = (ThreadInput*)data;
    ThreadPool* thread_pool = input->thread_pool;
    U32 thread_id = input->thread_id;

    os_set_thread_name(PushStr8F(scratch.arena, "ThreadWorker: %zu", thread_id));
    ThreadInfo thread_info = {};
    t_thread_pool = thread_pool;
    t_cur_thread_id = thread_id;
    thread_info.thread_pool = thread_pool;
    thread_info.thread_id = thread_id;

    for (;;)
    {
        if (thread_pool->kill_switch)
        {
            break;
        }

        U64 work_generation = thread_pool->work_generation.load(std::memory_order_acquire);
        WorkerTask item = {};
        if (_thread_pool_try_get_work(thread_pool, &item))
        {
            thread_pool->in_flight_count.fetch_add(1);
            item.worker_func(thread_info, item.data);
            thread_pool->in_flight_count.fetch_sub(1);
            continue;
        }

        U64 next_deadline = _thread_pool_next_deadline(thread_pool);
        OS_MutexTake(thread_pool->work_mutex);
        if (!thread_pool->kill_switch && work_generation == thread_pool->work_generation.load(std::memory_order_acquire))
        {
            os_condition_variable_wait(thread_pool->work_cv, thread_pool->work_mutex, next_deadline);
        }
        OS_MutexDrop(thread_pool->work_mutex);
    }

    t_cur_thread_id = max_U32;
    t_thread_pool = 0;
}

static ThreadPool*
worker_threads_create(Arena* arena, U32 thread_count, U32 mpmc_queue_size)
{
    ThreadPool* thread_info = PushStruct(arena, ThreadPool);
    thread_info->thread_handles = BufferAlloc<OS_Handle>(arena, thread_count);
    thread_info->thread_count = thread_count;
    thread_info->kill_switch = 0;
    thread_info->work_generation.store(0);
    thread_info->in_flight_count.store(0);
    thread_info->pending_task_count.store(0);
    thread_info->timer_min_heap = async::async_heap_alloc<WorkerTask>();
    thread_info->mpmc_queue = queue_alloc<WorkerTask>(arena, mpmc_queue_size);
    thread_info->work_mutex = OS_MutexAlloc();
    thread_info->work_cv = os_condition_variable_alloc();

    for (U32 i = 0; i < thread_count; i++)
    {
        ThreadInput* input = PushStruct(arena, ThreadInput);
        input->thread_pool = thread_info;
        input->thread_id = i;
        thread_info->thread_handles.data[i] = OS_ThreadLaunch(thread_worker, input, NULL);
    }

    return thread_info;
}

static void
worker_threads_destroy(ThreadPool* thread_info)
{
    thread_info->kill_switch = 1;
    _thread_pool_wake_workers(thread_info, true);

    for (U32 i = 0; i < thread_info->thread_handles.size; i++)
    {
        OS_ThreadJoin(thread_info->thread_handles.data[i], max_U32);
    }

    queue_release(thread_info->mpmc_queue);
    os_condition_variable_release(thread_info->work_cv);
    OS_MutexRelease(thread_info->work_mutex);
    async::async_heap_release(thread_info->timer_min_heap);
}
} // namespace async

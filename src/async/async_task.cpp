namespace async
{

template <typename T>
g_internal WorkerResult
_worker_task_func(ThreadInfo thread_info, WorkerData data)
{
    AsyncTaskWork<T>* work = (AsyncTaskWork<T>*)data;
    AsyncTaskStatus<T>* task = work->data;
    AsyncTaskContinuation<T> continuation_func = work->func(thread_info, work->data);
    work->func = continuation_func.func;
    WorkerResult worker_result = {};
    if (work->func)
    {
        worker_result.next_task = WorkerItem(work, _worker_task_func<T>);
        worker_result.us_delay = continuation_func.us_delay;
    }

    if (task->error || !work->func)
    {
        // curl cleanup must finish before publishing `done`: once `done` is
        // observed, the consumer (async_task_is_done) is free to release the
        // arena backing both `task` and the http extension, which would turn
        // the cleanup below into a use-after-free.
        if (has_flag(task->ext_type, ExtensionType::Http))
        {
            AsyncHttpTaskState<T>* http_ctx = task->http_ext;
            _curl_context_cleanup(http_ctx->curl_ctx);
        }
        task->done.store(true, std::memory_order_release);
    }
    return worker_result;
}

template <typename T>
g_internal AsyncTaskStatus<T>*
_async_task_status_create(Arena* arena, ThreadPool* thread_pool, String8 name, T* data)
{
    AsyncTaskStatus<T>* task_status = PushStruct(arena, AsyncTaskStatus<T>);

    task_status->thread_pool = thread_pool;
    task_status->arena = arena;
    task_status->task_name = push_str8_copy(arena, name);
    task_status->user_data = data;
    return task_status;
}

template <typename T>
g_internal AsyncTaskStatus<T>*
async_task_run(AsyncTaskStatus<T>* task_status, ThreadPool* thread_pool, WorkerTaskFunc<T> func, S64 us_delay)
{
    AsyncTaskWork<T>* work = PushStruct(task_status->arena, AsyncTaskWork<T>);
    work->func = func;
    work->data = task_status;
    WorkerItem task_ext = WorkerItem(work, _worker_task_func<T>);

    task_status->started = true;
    B32 scheduled = thread_pool_push(thread_pool, &task_ext, us_delay);
    if (!scheduled)
    {
        task_status->done.store(true);
    }
    return task_status;
}

template <typename T>
g_internal AsyncTaskResult<T>
async_task_is_done(AsyncTaskStatus<T>* task)
{
    AsyncTaskResult<T> result = AsyncTaskResult<T>(task);
    if (result.done)
    {
        B32 errored = task->error.load();
        if (errored)
        {
            ERROR_LOG("Error in task: %.*s", str8_varg(task->task_name));
        }
        if (has_flag(task->ext_type, ExtensionType::Http))
        {
            AsyncHttpTaskState<T>* http_ctx = task->http_ext;
            if (http_ctx->error.has_error())
            {
                AsyncError* error = &http_ctx->error;
                INFO_LOG("%.*s error: %u", str8_varg(task->task_name), (U32)error->result);
                if (error->curl_code)
                {
                    INFO_LOG("%.*s error curl code: %u from type %u", str8_varg(task->task_name), error->curl_code, (U32)error->curl_code_type);
                }
            }
        }
        INFO_LOG("%.*s work complete", str8_varg(task->task_name));
    }

    return result;
}

template <typename T>
g_internal AsyncTaskStatus<T>*
async_task_run(ThreadPool* thread_pool, WorkerTaskFunc<T> func, T* data, String8 task_name, S64 us_delay)
{
    Arena* task_arena = arena_alloc();
    AsyncTaskStatus<T>* task_status = _async_task_status_create<T>(task_arena, thread_pool, task_name, data);
    return async_task_run(task_status, thread_pool, func, us_delay);
}

template <typename T>
g_internal AsyncTaskStatus<T>*
async_task_run(Arena* arena, ThreadPool* thread_pool, WorkerTaskFunc<T> func, T* data, String8 task_name, S64 us_delay)
{
    AsyncTaskStatus<T>* task_status = _async_task_status_create<T>(arena, thread_pool, task_name, data);
    return async_task_run(task_status, thread_pool, func, us_delay);
}

template <typename T>
g_internal AsyncTaskStatus<T>*
async_task_with_ext_run(Arena* arena, ThreadPool* thread_pool, WorkerTaskFunc<T> func, T* data, String8 task_name, S64 us_delay, ExtensionType ext_type, void* ext)
{
    AsyncTaskStatus<T>* task_status = _async_task_status_create<T>(arena, thread_pool, task_name, data);
    task_status->ext_type = ext_type;

    if (has_flag(ext_type, ExtensionType::Http) || (ext != 0))
    {
        task_status->http_ext = (AsyncHttpTaskState<T>*)ext;
    }

    return async_task_run(task_status, thread_pool, func, us_delay);
}

} // namespace async

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
        if (task->ext_type == ExtensionType::Http)
        {
            AsyncHttpTaskState<T>* http_ctx = task->http_ext;
            _curl_context_cleanup(&http_ctx->curl_ctx);
        }
        task->done.store(true, std::memory_order_release);
    }
    return worker_result;
}

template <typename T>
g_internal AsyncTaskStatus<T>*
_async_task_status_create(Arena* arena, String8 name, T* data)
{
    AsyncTaskStatus<T>* task_status = PushStruct(arena, AsyncTaskStatus<T>);
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
g_internal bool
async_task_is_done(AsyncTaskStatus<T>* task, T** out_result, B32* out_success)
{
    B32 task_done = task->done.load(std::memory_order_acquire);
    if (task_done)
    {
        B32 errored = task->error.load();
        if (errored)
        {
            ERROR_LOG("Error in task: %.*s", str8_varg(task->task_name));
        }
        switch (task->ext_type)
        {
            case ExtensionType::Http:
            {
                AsyncHttpTaskState<T>* async_ctx = task->http_ext;
                const AsyncHttpResult& http_result = async_ctx->http_result;
                if (http_result.async_result != AsyncResult::Success)
                {
                    INFO_LOG("%.*s error: %u", str8_varg(task->task_name), (U32)http_result.async_result);
                    if (http_result.error_code)
                    {
                        INFO_LOG("%.*s error code: %u", str8_varg(task->task_name), http_result.error_code);
                    }
                    if (http_result.error_str.size > 0)
                    {
                        INFO_LOG("%.*s error msg: %.*s", str8_varg(task->task_name), str8_varg(http_result.error_str));
                    }
                }
            }
            break;
        }
        INFO_LOG("%.*s work successfully complete", str8_varg(task->task_name));
        *out_result = task->user_data;
        if (out_success)
        {
            *out_success = !errored;
        }
        arena_release(task->arena);
    }
    return task_done;
}

template <typename T>
g_internal WorkerTaskFunc<T>
async_task_done(AsyncTaskStatus<T>* task_status, bool success)
{
    task_status->success.store(success, std::memory_order_release);
    task_status->done.store(true, std::memory_order_release);
    return {};
}

template <typename T>
g_internal AsyncTaskStatus<T>*
async_task_run(ThreadPool* thread_pool, WorkerTaskFunc<T> func, T* data, String8 task_name, S64 us_delay)
{
    Arena* task_arena = arena_alloc();
    AsyncTaskStatus<T>* task_status = _async_task_status_create<T>(task_arena, task_name, data);
    return async_task_run(task_status, thread_pool, func, us_delay);
}

template <typename T>
g_internal AsyncTaskStatus<T>*
async_task_run(Arena* arena, ThreadPool* thread_pool, WorkerTaskFunc<T> func, T* data, String8 task_name, S64 us_delay)
{
    AsyncTaskStatus<T>* task_status = _async_task_status_create<T>(arena, task_name, data);
    return async_task_run(task_status, thread_pool, func, us_delay);
}

template <typename T>
g_internal AsyncTaskStatus<T>*
async_task_with_ext_run(Arena* arena, ThreadPool* thread_pool, WorkerTaskFunc<T> func, T* data, String8 task_name, S64 us_delay, ExtensionType ext_type, void* ext)
{
    AsyncTaskStatus<T>* task_status = _async_task_status_create<T>(arena, task_name, data);
    task_status->ext_type = ext_type;

    switch (ext_type)
    {
        case ExtensionType::Http: task_status->http_ext = (AsyncHttpTaskState<T>*)ext; break;
        default: InvalidPath;
    }
    return async_task_run(task_status, thread_pool, func, us_delay);
}

} // namespace async

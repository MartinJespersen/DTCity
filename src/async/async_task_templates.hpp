#pragma once

#include "async_task.hpp"
#include "async_http.hpp"

// Template implementations
namespace async
{

template <typename T>
void
AsyncTaskStatus<T>::add_error()
{
    this->error = true;
}
template <typename T>
void
AsyncTaskStatus<T>::add_error(const AsyncGenericError& new_error)
{
    if (has_flag(this->ext_type, ExtensionType::Http))
    {
        this->http_ext->error_attach(new_error);
    }
    this->add_error();
}

template <typename T>
WorkerResult
_worker_task_func(ThreadInfo thread_info, WorkerData data)
{
    AsyncTaskWork<T>* work = (AsyncTaskWork<T>*)data;
    AsyncTaskStatus<T>* task = work->data;
    AsyncTaskContinuation<T> continuation_func = work->func(thread_info, work->data);
    work->func = continuation_func.func;
    WorkerResult worker_result = {};
    if (work->func && !task->error)
    {
        worker_result.next_task = WorkerItem(work, _worker_task_func<T>);
        worker_result.us_delay = continuation_func.us_delay;
    }

    if (task->error || !work->func)
    {
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
AsyncTaskStatus<T>*
_async_task_status_create(Allocator allocator, ThreadPool* thread_pool, String8 name, T* data)
{
    AsyncTaskStatus<T>* task_status = new AsyncTaskStatus<T>{std::move(allocator)};

    task_status->thread_pool = thread_pool;
    task_status->task_name = push_str8_copy(task_status->allocator.arena, name);
    task_status->user_data = data;
    return task_status;
}

template <typename T>
AsyncTaskStatus<T>*
async_task_run(AsyncTaskStatus<T>* task_status, ThreadPool* thread_pool, WorkerTaskFunc<T> func, S64 us_delay)
{
    AsyncTaskWork<T>* work = task_status->allocator.template make<AsyncTaskWork<T>>();
    work->func = func;
    work->data = task_status;
    WorkerItem task_ext = WorkerItem(work, _worker_task_func<T>);

    task_status->started = true;
    B32 scheduled = thread_pool_push(thread_pool, &task_ext, us_delay);
    if (!scheduled)
    {
        task_status->add_error();
        if (has_flag(task_status->ext_type, ExtensionType::Http))
        {
            _curl_context_cleanup(task_status->http_ext->curl_ctx);
        }
        task_status->done.store(true, std::memory_order_release);
    }
    return task_status;
}

template <typename T>
std::unique_ptr<AsyncTaskStatus<T>>
async_task_take_completed(AsyncTaskStatus<T>* task)
{
    std::unique_ptr<AsyncTaskStatus<T>> result;
    B32 expected = true;
    if (task->done.compare_exchange_strong(expected, false, std::memory_order_acquire, std::memory_order_relaxed))
    {
        result.reset(task);
        B32 errored = task->error;
        if (errored)
        {
            ERROR_LOG("Error in task: %.*s", str8_varg(task->task_name));
        }
        if (has_flag(task->ext_type, ExtensionType::Http))
        {
            AsyncHttpTaskState<T>* http_ctx = task->http_ext;
            if (http_ctx->error.has_error())
            {
                auto* error = &http_ctx->error;
                INFO_LOG("%.*s error: %u", str8_varg(task->task_name), (U32)error->result);
                if (error->error_code)
                {
                    INFO_LOG("%.*s error code: %u (%.*s)", str8_varg(task->task_name), error->error_code, (int)error->error_str.size(), error->error_str.c_str());
                }
            }
        }
        INFO_LOG("%.*s work complete", str8_varg(task->task_name));
    }

    return result;
}

template <typename T>
AsyncTaskStatus<T>*
async_task_run(ThreadPool* thread_pool, WorkerTaskFunc<T> func, T* data, const char* task_name, S64 us_delay)
{
    Allocator allocator = Allocator::create();
    Debug_SetName(allocator.arena, task_name);
    return async_task_run(std::move(allocator), thread_pool, func, data, task_name, us_delay);
}

template <typename T>
AsyncTaskStatus<T>*
async_task_run(Allocator allocator, ThreadPool* thread_pool, WorkerTaskFunc<T> func, T* data, const char* task_name, S64 us_delay)
{
    String8 task_name_str8 = str8_c_string(task_name);
    AsyncTaskStatus<T>* task_status = _async_task_status_create<T>(std::move(allocator), thread_pool, task_name_str8, data);
    return async_task_run(task_status, thread_pool, func, us_delay);
}

template <typename T>
AsyncTaskStatus<T>*
async_task_with_ext_run(Allocator allocator, ThreadPool* thread_pool, WorkerTaskFunc<T> func, T* data, const char* task_name, S64 us_delay, ExtensionType ext_type, void* ext)
{
    String8 task_name_str8 = str8_c_string(task_name);
    AsyncTaskStatus<T>* task_status = _async_task_status_create<T>(std::move(allocator), thread_pool, task_name_str8, data);
    task_status->ext_type = ext_type;

    if (has_flag(ext_type, ExtensionType::Http) || (ext != 0))
    {
        task_status->http_ext = (AsyncHttpTaskState<T>*)ext;
    }

    return async_task_run(task_status, thread_pool, func, us_delay);
}
} // namespace async

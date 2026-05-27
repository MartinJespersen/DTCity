#pragma once
namespace async
{

template <typename T>
struct AsyncHttpTaskState;

enum class ExtensionType
{
    None,
    Http
};

template <typename T>
struct AsyncTaskStatus
{
    Arena* arena;
    String8 task_name;

    ExtensionType ext_type;
    union
    {
        AsyncHttpTaskState<T>* http_ext;
    };

    B32 started;
    std::atomic<B32> done;
    std::atomic<B32> error;

    T* user_data;
};

template <typename T>
struct AsyncTaskContinuation;

template <typename T>
using WorkerTaskFunc = AsyncTaskContinuation<T> (*)(ThreadInfo, AsyncTaskStatus<T>*);

template <typename T>
struct AsyncTaskContinuation
{
    WorkerTaskFunc<T> func;
    S64 us_delay;
};

template <typename T>
struct AsyncTaskWork
{
    WorkerTaskFunc<T> func;
    AsyncTaskStatus<T>* data;
};

template <typename T>
g_internal AsyncTaskStatus<T>*
async_task_run(AsyncTaskStatus<T>* task_status, ThreadPool* thread_pool, WorkerTaskFunc<T> func, S64 us_delay);

template <typename T>
g_internal AsyncTaskStatus<T>*
_async_task_status_create(String8 name);

template <typename T>
g_internal bool
async_task_is_done(AsyncTaskStatus<T>* task, T** out_result, B32* out_success = nullptr);

template <typename T>
g_internal AsyncTaskStatus<T>*
async_task_run(ThreadPool* thread_pool, WorkerTaskFunc<T> func, T* data, String8 task_name, S64 us_delay = 0);

template <typename T>
g_internal AsyncTaskStatus<T>*
async_task_run(Arena* arena, ThreadPool* thread_pool, WorkerTaskFunc<T> func, T* data, String8 task_name, S64 us_delay);

template <typename T>
g_internal AsyncTaskStatus<T>*
async_task_with_ext_run(Arena* arena, ThreadPool* thread_pool, WorkerTaskFunc<T> func, T* data, String8 task_name, S64 us_delay, ExtensionType ext_type, void* ext);
} // namespace async

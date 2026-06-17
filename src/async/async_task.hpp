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

} // namespace async

template <>
inline constexpr bool enable_bitmask<async::ExtensionType> = true;

namespace async
{

template <typename T>
struct AsyncTaskStatus
{
    Arena* arena;
    String8 task_name;
    ThreadPool* thread_pool;

    ExtensionType ext_type;
    AsyncHttpTaskState<T>* http_ext;

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
struct AsyncTaskResult
{
    AsyncTaskResult()
    {
        this->task = 0;
        this->done = false;
        this->success = false;
    }

    AsyncTaskResult(AsyncTaskStatus<T>* task)
    {
        this->task = 0;
        this->done = false;
        this->success = false;

        B32 expected = 1;
        if (task->done.compare_exchange_strong(expected, 0))
        {
            this->done = true;
            this->task = task;
            this->success = !task->error.load(std::memory_order_acquire);
        }
    }

    AsyncTaskResult(const AsyncTaskResult<T>& other) = delete;
    AsyncTaskResult<T>&
    operator=(const AsyncTaskResult<T>& other) = delete;

    AsyncTaskResult(AsyncTaskResult<T>&& other) noexcept
    {
        this->task = other.task;
        this->done = other.done;
        this->success = other.success;

        other.task = 0;
        other.done = false;
        other.success = false;
    }

    AsyncTaskResult<T>&
    operator=(AsyncTaskResult<T>&& other) noexcept
    {
        if (this != &other)
        {
            if (this->task)
            {
                arena_release(this->task->arena);
            }

            this->task = other.task;
            this->done = other.done;
            this->success = other.success;

            other.task = 0;
            other.done = false;
            other.success = false;
        }
        return *this;
    }

    ~AsyncTaskResult()
    {
        if (this->task)
        {
            arena_release(this->task->arena);
        }
    }

    AsyncTaskStatus<T>* task;
    B32 done;
    B32 success;
};

template <typename T>
g_internal AsyncTaskStatus<T>*
async_task_run(AsyncTaskStatus<T>* task_status, ThreadPool* thread_pool, WorkerTaskFunc<T> func, S64 us_delay);

template <typename T>
g_internal AsyncTaskStatus<T>*
_async_task_status_create(String8 name);

template <typename T>
g_internal AsyncTaskResult<T>
async_task_is_done(AsyncTaskStatus<T>* task);

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

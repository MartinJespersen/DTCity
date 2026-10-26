#pragma once

#include "thread_pool.hpp"
#include <memory>
namespace async
{

template <typename T>
struct AsyncHttpTaskState;
struct HttpInfo;
struct AsyncGenericError;

enum class ExtensionType
{
    None,
    Http = (1 << 0)
};

constexpr bool
enable_bitmask(ExtensionType)
{
    return true;
}

template <typename T>
struct AsyncTaskStatus
{
    Allocator allocator;
    String8 task_name;
    ThreadPool* thread_pool;

    ExtensionType ext_type;
    AsyncHttpTaskState<T>* http_ext;

    B32 started;
    std::atomic<B32> done;
    // Written by the executing task; readers acquire done before reading this flag.
    B32 error;

    T* user_data;

    void
    add_error();
    void
    add_error(const AsyncGenericError& new_error);
    // Reset and configure an HTTP task; return true and mark the task on failure.
    Error
    http_reconfigure(HttpInfo* http_info);
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
AsyncTaskStatus<T>*
async_task_run(AsyncTaskStatus<T>* task_status, ThreadPool* thread_pool, WorkerTaskFunc<T> func, S64 us_delay);

template <typename T>
AsyncTaskStatus<T>*
_async_task_status_create(Allocator allocator, ThreadPool* thread_pool, String8 name, T* data);

template <typename T>
[[nodiscard]] std::unique_ptr<AsyncTaskStatus<T>>
async_task_take_completed(AsyncTaskStatus<T>* task);

template <typename T>
AsyncTaskStatus<T>*
async_task_run(ThreadPool* thread_pool, WorkerTaskFunc<T> func, T* data, const char* task_name, S64 us_delay = 0);

template <typename T>
AsyncTaskStatus<T>*
async_task_run(Allocator allocator, ThreadPool* thread_pool, WorkerTaskFunc<T> func, T* data, const char* task_name, S64 us_delay = 0);

template <typename T>
AsyncTaskStatus<T>*
async_task_with_ext_run(Allocator allocator, ThreadPool* thread_pool, WorkerTaskFunc<T> func, T* data, const char* task_name, S64 us_delay, ExtensionType ext_type, void* ext);
} // namespace async

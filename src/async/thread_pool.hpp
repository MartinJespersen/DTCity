#pragma once

namespace async
{
template <typename T> struct Heap;
template <typename T> struct Queue;
struct ThreadPool;
struct ThreadInfo;
typedef void (*WorkerFunc)(ThreadInfo, void*);
struct WorkerTask
{
    void* data;
    WorkerFunc worker_func;
};
struct ThreadInfo
{
    ThreadPool* thread_pool;
    U32 thread_id;
};

struct ThreadInput
{
    ThreadPool* thread_pool;
    U32 thread_id;
};

struct ThreadPool
{
    B32 kill_switch;
    U32 thread_count;

    // Workers read immediate tasks from the shared queue and delayed tasks from the timer heap.
    std::atomic<U64> work_generation;
    std::atomic<U32> in_flight_count;
    std::atomic<U32> pending_task_count;
    Buffer<OS_Handle> thread_handles;
    Heap<WorkerTask>* timer_min_heap;
    Queue<WorkerTask>* mpmc_queue;
    OS_Handle work_mutex;
    OS_Handle work_cv;
};

// ~mgj: Globals /////////////////////////////
// used for threads that want to access thread_local data
thread_local U32 t_cur_thread_id = max_U32;
thread_local ThreadPool* t_thread_pool = 0;
////////////////////////////////////////////////
static U64
_thread_pool_next_deadline(ThreadPool* thread_pool);
static B32
_thread_pool_try_get_work(ThreadPool* thread_pool, WorkerTask* item);
static void
_thread_pool_wake_workers(ThreadPool* thread_pool, B32 wake_all);
static B32
thread_pool_register_current_thread(ThreadPool* thread_pool);
static B32
thread_pool_push(ThreadPool* thread_pool, WorkerTask* item, S64 us_delay = 0);
static B32
thread_pool_has_pending_work(ThreadPool* thread_pool);
static void
thread_worker(void* data);
static ThreadPool*
worker_threads_create(Arena* arena, U32 thread_count, U32 mpmc_queue_size);
static void
worker_threads_destroy(ThreadPool* thread_info);

} // namespace async

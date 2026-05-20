#pragma once

namespace async
{
template <typename T>
struct Heap;
template <typename T>
struct Queue;
struct ThreadPool;
struct ThreadInfo;

struct WorkerResult;
typedef void* WorkerData;
typedef WorkerResult (*WorkerFunc)(ThreadInfo, WorkerData);

struct WorkerItem
{
    WorkerData user_data;
    WorkerFunc func;

    WorkerItem() = default;
    WorkerItem(WorkerData user_data, WorkerFunc func) : user_data(user_data), func(func)
    {
    }
};

struct WorkerResult
{
    WorkerItem next_task;
    S64 us_delay;
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

    // worker thread queues
    // Workers read immediate tasks from the shared queue and delayed tasks from the timer heap.
    U32 thread_count;
    std::atomic<U64> work_generation;
    std::atomic<U32> in_flight_count;
    std::atomic<U32> pending_task_count;
    Buffer<OS_Handle> thread_handles;
    Heap<WorkerItem>* timer_min_heap;
    Queue<WorkerItem>* mpmc_queue;
    OS_Handle work_mutex;
    OS_Handle work_cv;

    // main thread queue: main thread pulls and worker thread push to this queue
    Queue<WorkerItem>* main_thread_queue;
    OS_Handle main_thread_queue_mutex;
    OS_Handle main_thread_queue_cv;
};

// ~mgj: Globals /////////////////////////////
// used for threads that want to access thread_local data
thread_local U32 t_cur_thread_id = max_U32;
thread_local ThreadPool* t_thread_pool = 0;
////////////////////////////////////////////////
static U64
_thread_pool_next_deadline(ThreadPool* thread_pool);
static B32
_thread_pool_try_get_work(ThreadPool* thread_pool, WorkerItem* item);
static void
_thread_pool_worker_task_execute(ThreadInfo thread_info, WorkerItem* item);
static void
_thread_pool_wake_workers(ThreadPool* thread_pool, B32 wake_all);
static B32
thread_pool_register_current_thread(ThreadPool* thread_pool);
static B32
thread_pool_push(ThreadPool* thread_pool, WorkerItem* task, S64 us_delay = 0);
static B32
thread_pool_has_pending_work(ThreadPool* thread_pool);
static void
thread_worker(void* data);
static ThreadPool*
thread_pool_create(Arena* arena, U32 thread_count, U32 mpmc_queue_size, U32 main_thread_queue_size);
static void
thread_pool_destroy(ThreadPool* thread_info);

// main thread queue functions
static B32
thread_pool_main_thread_queue_push(ThreadPool* thread_pool, WorkerItem* item);
static B32
thread_pool_main_thread_queue_try_pull(ThreadPool* thread_pool, WorkerItem* item);
static void
thread_pool_main_thread_queue_drain(ThreadPool* thread_pool);
} // namespace async

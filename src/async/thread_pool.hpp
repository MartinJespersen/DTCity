#pragma once

#include "spmc_queue.hpp"

namespace async
{
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

    // Each thread can produce and consume to own FIFO queue and able to consume from the queue of other threads
    std::atomic<U32> in_flight_count;
    std::atomic<U32> pending_task_count;
    std::atomic<U32> submitter_thread_os_id;
    Buffer<SpmcQueue<WorkerTask>*> worker_queues;
    Buffer<OS_Handle> thread_handles;
    Heap<WorkerTask>* timer_min_heap;
    OS_Handle work_semaphore;

    // Similar to the FIFO queue above, each thread also produce and consume work from a single min-heap to allow for asynchronously sleep.
    // This is beneficial for cases where we have to wait too long for a result to arrive.
    // Buffer<>
};

// ~mgj: Globals /////////////////////////////
// used for threads that want to access thread_local data
thread_local U32 t_cur_thread_id = max_U32;
thread_local U32 t_cur_queue_id = max_U32;
thread_local ThreadPool* t_thread_pool = 0;
////////////////////////////////////////////////
static B32
thread_pool_register_current_thread(ThreadPool* thread_pool);
static B32
thread_pool_try_push(ThreadPool* thread_pool, WorkerTask* item, S64 microsecond_delay = 0);
static void
thread_pool_push(ThreadPool* thread_pool, WorkerTask* item, S64 microsecond_delay = 0);
static B32
thread_pool_has_pending_work(ThreadPool* thread_pool);
static void
thread_worker(void* data);
static ThreadPool*
worker_threads_create(Arena* arena, U32 thread_count, U32 queue_size);
static void
worker_threads_destroy(ThreadPool* thread_info);

}; // namespace async

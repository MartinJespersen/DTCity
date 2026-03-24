#pragma once

#include "spmc_queue.hpp"

namespace async
{
struct Threads;
struct ThreadInfo;
typedef void (*WorkerFunc)(ThreadInfo, void*);
struct QueueItem
{
    void* data;
    WorkerFunc worker_func;
};
struct ThreadInfo
{
    Threads* thread_pool;
    U32 thread_id;
};

struct ThreadInput
{
    Threads* thread_pool;
    U32 thread_id;
};

struct Threads
{
    B32 kill_switch;
    U32 thread_count;
    std::atomic<U32> in_flight_count;
    std::atomic<U32> pending_task_count;
    std::atomic<U32> submitter_thread_os_id;
    Buffer<SpmcQueue<QueueItem>*> worker_queues;
    Buffer<OS_Handle> thread_handles;
    OS_Handle work_semaphore;
};

// ~mgj: Globals /////////////////////////////
// used for threads that want to access thread_local data
thread_local U32 t_cur_thread_id = max_U32;
thread_local U32 t_cur_queue_id = max_U32;
thread_local Threads* t_thread_pool = 0;
////////////////////////////////////////////////
static B32
thread_pool_register_current_thread(Threads* thread_pool);
static B32
thread_pool_try_push(Threads* thread_pool, QueueItem* item);
static void
thread_pool_push(Threads* thread_pool, QueueItem* item);
static B32
thread_pool_has_pending_work(Threads* thread_pool);
static void
ThreadWorker(void* data);
static Threads*
WorkerThreadsCreate(Arena* arena, U32 thread_count, U32 queue_size);
static void
WorkerThreadsDestroy(Threads* thread_info);

}; // namespace async

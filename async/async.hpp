#pragma once
namespace async
{
struct ThreadInfo;
typedef void (*WorkerFunc)(ThreadInfo, void*);
struct QueueItem
{
    void* data;
    WorkerFunc worker_func;
};

struct Queue
{
    volatile U32 next_index;
    volatile U32 fill_index;
    U32 queue_size;
    QueueItem* items;
    OS_Handle mutex;
    OS_Handle semaphore_empty;
    OS_Handle semaphore_full;
};
struct ThreadInfo
{
    Queue* queue;
    U32 thread_id;
};

struct ThreadInput
{
    Queue* queue;
    U32 thread_count;
    U32 thread_id;
    B32* kill_switch;
};

struct Threads
{
    B32 kill_switch;
    async::Queue* msg_queue;
    Buffer<OS_Handle> thread_handles;
};

static Queue*
QueueInit(Arena* arena, U32 queue_size, U32 thread_count);
static void
QueueDestroy(Queue* queue);
static void
QueuePush(Queue* queue, void* data, WorkerFunc worker_func);
static void
ThreadWorker(void* data);
static Threads*
WorkerThreadsCreate(Arena* arena, U32 thread_count, U32 queue_size);
static void
WorkerThreadDestroy(Threads* thread_info);

}; // namespace async

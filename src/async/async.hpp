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
template <typename T> struct Queue
{
    volatile U32 next_index;
    volatile U32 fill_index;
    U32 queue_size;
    T* items;
    OS_Handle mutex;
    OS_Handle semaphore_empty;
    OS_Handle semaphore_full;
};
struct ThreadInfo
{
    Queue<QueueItem>* queue;
    U32 thread_id;
};

struct ThreadInput
{
    Queue<QueueItem>* queue;
    U32 thread_count;
    U32 thread_id;
    B32* kill_switch;
    std::atomic<U32>* in_flight_count;
};

struct Threads
{
    B32 kill_switch;
    std::atomic<U32> in_flight_count;
    async::Queue<QueueItem>* msg_queue;
    Buffer<OS_Handle> thread_handles;
};

// ~mgj: Globals /////////////////////////////
// used for threads that want to access thread_local data
thread_local U32 t_cur_thread_id = 0;
////////////////////////////////////////////////
template <typename T>
static Queue<T>*
QueueInit(Arena* arena, U32 queue_size, U32 thread_count);
template <typename T>
static void
QueueDestroy(Queue<T>* queue);
template <typename T>
static B32
QueueTryRead(Queue<T>* queue, T* item);
template <typename T>
static B32
QueueTryPush(Queue<T>* queue, T* data);
template <typename T>
static void
QueuePush(Queue<T>* queue, T* data);
static void
ThreadWorker(void* data);
static Threads*
WorkerThreadsCreate(Arena* arena, U32 thread_count, U32 queue_size);
static void
WorkerThreadsDestroy(Threads* thread_info);

}; // namespace async

namespace async
{

struct AsyncArena
{
    Arena* arena;
    OS_Handle mutex;
};

g_internal AsyncArena*
async_arena_alloc();
g_internal void
async_arena_release(async::AsyncArena* async_arena);
g_internal void*
async_arena_push(AsyncArena* async_arena, U32 size, U32 align);

#define AsyncPushArrayNoZeroAligned(a, T, c, align) (T*)async_arena_push((a), sizeof(T) * (c), (align))
#define AsyncPushArrayAligned(a, T, c, align) (T*)MemoryZero(AsyncPushArrayNoZeroAligned(a, T, c, align), sizeof(T) * (c)) // NOLINT(bugprone-sizeof-expression)
#define AsyncPushArrayNoZero(a, T, c) AsyncPushArrayNoZeroAligned(a, T, c, Max(8, AlignOf(T)))
#define AsyncPushArray(a, T, c) AsyncPushArrayAligned(a, T, c, Max(8, AlignOf(T)))
#define AsyncPushStruct(a, T) AsyncPushArrayAligned(a, T, 1, Max(8, AlignOf(T)))
} // namespace async

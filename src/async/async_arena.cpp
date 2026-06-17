namespace async
{

g_internal AsyncArena*
async_arena_alloc()
{
    Arena* arena = arena_alloc();
    AsyncArena* async_arena = PushStruct(arena, AsyncArena);
    async_arena->arena = arena;
    async_arena->mutex = os_rw_mutex_alloc();
    return async_arena;
}

g_internal void
async_arena_release(async::AsyncArena* async_arena)
{
    os_rw_mutex_release(async_arena->mutex);
    arena_release(async_arena->arena);
}

g_internal void*
async_arena_push(AsyncArena* async_arena, U32 size, U32 align)
{
    void* ptr = 0;
    os_mutex_scope_w(async_arena->mutex)
    {
        ptr = arena_push(async_arena->arena, size, align);
    }
    return ptr;
}
} // namespace async

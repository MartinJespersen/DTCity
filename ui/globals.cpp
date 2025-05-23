// Global Context
Context* g_ctx;
ThreadCtx* g_thread_ctx;

C_LINKAGE void
GlobalContextSet(Context* ctx)
{
    w32_entry_point_caller(__argc, __wargv);
    g_ctx = ctx;
    g_thread_ctx = ctx->thread_ctx;
}

internal Context*
GlobalContextGet()
{
    return g_ctx;
}

// Thread Context ------------------------------------------------------------

internal ThreadCtx*
ThreadCtxGet()
{
    return g_thread_ctx;
}

internal void
ThreadContextInit()
{
    for (U32 tctx_i = 0; tctx_i < ArrayCount(g_thread_ctx->scratchArenas); tctx_i++)
    {
        g_thread_ctx->scratchArenas[tctx_i] = arena_alloc();
    }
}

internal void
ThreadContextExit()
{
    for (U32 tctx_i = 0; tctx_i < ArrayCount(g_thread_ctx->scratchArenas); tctx_i++)
    {
        arena_release(g_thread_ctx->scratchArenas[tctx_i]);
    }
}

internal Temp
ArenaScratchGet()
{
    Temp temp = {};
    temp.pos = g_thread_ctx->scratchArenas[0]->pos;
    temp.arena = g_thread_ctx->scratchArenas[0];
    return temp;
}

internal Temp
ArenaScratchGet(Arena** conflicts, U64 conflict_count)
{
    Temp scratch = {0};
    ThreadCtx* tctx = g_thread_ctx;
    for (U64 tctx_idx = 0; tctx_idx < ArrayCount(tctx->scratchArenas); tctx_idx += 1)
    {
        B32 is_conflicting = 0;
        for (Arena** conflict = conflicts; conflict < conflicts + conflict_count; conflict += 1)
        {
            if (*conflict == tctx->scratchArenas[tctx_idx])
            {
                is_conflicting = 1;
                break;
            }
        }
        if (is_conflicting == 0)
        {
            scratch.arena = tctx->scratchArenas[tctx_idx];
            scratch.pos = scratch.arena->pos;
            break;
        }
    }
    return scratch;
}
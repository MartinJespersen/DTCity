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

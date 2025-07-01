// Global Context
global Context* g_ctx;

C_LINKAGE void
GlobalContextSet(Context* ctx)
{
    g_ctx = ctx;
    os_w32_state = *ctx->os_w32_state;
}

internal Context*
GlobalContextGet()
{
    return g_ctx;
}

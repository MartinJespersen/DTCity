// Global Context
Context* g_ctx;

C_LINKAGE void
GlobalContextSet(Context* ctx)
{
    g_ctx = ctx;
}

internal Context*
GlobalContextGet()
{
    return g_ctx;
}

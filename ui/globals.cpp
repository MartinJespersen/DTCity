// Global Context
static Context* g_ctx;

C_LINKAGE void
GlobalContextSet(Context* ctx)
{
    g_ctx = ctx;
}

static Context*
GlobalContextGet()
{
    return g_ctx;
}

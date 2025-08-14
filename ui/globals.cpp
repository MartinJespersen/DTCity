// Global Context
static Context* g_ctx;

C_LINKAGE void
GlobalContextSet(Context* ctx)
{
    g_ctx = ctx;
}

static void
ThreadCtxSet(Context* ctx)
{
    g_ctx = ctx;
    wrapper::VulkanCtxSet(ctx->vk_ctx);
}

static Context*
GlobalContextGet()
{
    return g_ctx;
}

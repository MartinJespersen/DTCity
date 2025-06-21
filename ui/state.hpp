struct ProfilingContext
{
#ifdef PROFILING_ENABLE
    // tracy profiling context
    Buffer<TracyVkCtx> tracyContexts;
#endif
};

struct UI_IO
{
    Vec2F64 mousePosition;
    bool leftClicked;
};

// Threading Context
struct ThreadCtx
{
    Arena* scratchArenas[2];
};

struct Terrain;
struct Context
{
    Arena* arena_permanent;
    Terrain* terrain;
    VulkanContext* vulkanContext;
    ProfilingContext* profilingContext;
    UI_IO* io;
    ThreadCtx* thread_ctx;
    const char* cwd = "C:\\repos\\DTCity";
};

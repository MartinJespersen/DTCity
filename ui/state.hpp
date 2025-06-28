struct ProfilingContext
{
#ifdef TRACY_ENABLE
    // tracy profiling context
    Buffer<TracyVkCtx> tracyContexts;
#endif
};

// Threading Context
struct ThreadCtx
{
    Arena* scratchArenas[2];
};

struct Terrain;
struct Context
{
    B32 running;

    Arena* arena_permanent;
    Terrain* terrain;

    VulkanContext* vulkanContext;
    ProfilingContext* profilingContext;
    UI_IO* io;
    const char* cwd = "C:\\repos\\DTCity";

    glm::mat4 view_matrix;
    glm::mat4 projection_matrix;
};

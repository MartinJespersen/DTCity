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

struct DllInfo
{
    const char* func_name;
    const char* cleanup_func_name;
    const char* dll_path;
    const char* dll_temp_path;
    HMODULE handle;
    FILETIME last_modified;
    OS_Handle (*func)(void*);
    void (*cleanup_func)(void*);
};

struct Terrain;
struct UI_Camera;
struct Context
{
    B32 running;

    DllInfo* dll_info;
    OS_Handle main_thread_handle;
    OS_W32_State* os_w32_state;

    Arena* arena_permanent;
    Terrain* terrain;

    VulkanContext* vulkanContext;
    ProfilingContext* profilingContext;
    IO* io;
    UI_Camera* camera;
    // TODO: CWD should be set in init function
    const char* cwd = "C:\\repos\\DTCity";
};

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

struct DT_Time
{
    F32 delta_time_sec;
    U64 last_time_ms;
};

struct Terrain;
namespace ui
{
struct Camera;
}
struct Context
{
    B32 running;

    DllInfo* dll_info;
    OS_Handle main_thread_handle;
    OS_W32_State* os_w32_state;

    Arena* arena_permanent;
    Terrain* terrain;

    wrapper::VulkanContext* vk_ctx;
    ProfilingContext* profilingContext;
    IO* io;
    ui::Camera* camera;
    DT_Time* time;
    city::City* city;
    // TODO: CWD should be set in init function
    const char* cwd = "C:\\repos\\DTCity";
};

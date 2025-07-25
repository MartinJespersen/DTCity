struct ProfilingContext
{
#ifdef TRACY_ENABLE
    // tracy profiling context
    Buffer<TracyVkCtx> tracyContexts;
#endif
};

struct DllInfo
{
    const char* func_name;
    const char* cleanup_func_name;
    String8 dll_path;
    String8 dll_temp_path;
    void* dll_handle;
    OS_Handle entrypoint_thread_handle;
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
    String8 cwd;
    String8 texture_path;

    DllInfo* dll_info;

    Arena* arena_permanent;
    Terrain* terrain;

    wrapper::VulkanContext* vk_ctx;
    ProfilingContext* profilingContext;
    IO* io;
    ui::Camera* camera;
    DT_Time* time;
    city::City* city;

    // TODO: change this. moves os global state for use in DLL
    void* os_state;
};

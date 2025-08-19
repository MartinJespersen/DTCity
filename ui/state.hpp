// ~mgj: Forward declarations
struct Terrain;
namespace ui
{
struct Camera;
}

struct DT_Time
{
    F32 delta_time_sec;
    U64 last_time_ms;
};

struct Context
{
    B32 running;
    String8 cwd;
    String8 cache_path;

    Arena* arena_permanent;

    wrapper::VulkanContext* vk_ctx;
    IO* io;
    ui::Camera* camera;
    DT_Time* time;
    city::Road* road;
    city::CarSim* car_sim;

    async::Threads* thread_info;
    // TODO: change this. moves os global state for use in DLL
    void* os_state;
};

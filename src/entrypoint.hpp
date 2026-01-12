#pragma once

struct dt_Time
{
    F64 delta_time_sec;
    U64 last_time_ms;
};

struct dt_Input
{
    Rng2F64 bbox;
};

enum dt_DataDirType
{
    Cache,
    Texture,
    Shaders,
    Assets,
    Count
};

struct dt_DataDirPair
{
    dt_DataDirType type;
    String8 name;
};

struct Context
{
    B32 running;
    String8 cwd;
    String8 data_dir;
    Buffer<String8> data_subdirs;

    Arena* arena_permanent;

    io::IO* io;
    ui::Camera* camera;
    dt_Time* time;
    city::Road* road;
    city::CarSim* car_sim;
    city::Buildings* buildings;

    async::Threads* thread_pool;
};

// ~mgj: Globals
static Context* g_ctx;
const U32 MAX_FONTS_IN_USE = 10;

// globals context
static void
dt_ctx_set(Context* ctx);
static Context*
dt_ctx_get();

static dt_Input
dt_interpret_input(int argc, char** argv);
static OS_Handle
dt_render_thread_start(void* ptr);
static void
dt_render_thread_join(OS_Handle thread_handle, void* ptr);
static void
dt_imgui_setup(vulkan::Context* vk_ctx, io::IO* io_ctx);
static void
dt_main_loop(void* ptr);

static Buffer<String8>
dt_dir_create(Arena* arena, String8 parent, dt_DataDirPair* dirs, U32 count);

#pragma once

struct dt_Time
{
    F64 delta_time_sec;
    U64 last_time_ms;
};

struct dt_Input
{
    String8 tileset_url;
    Vec2F64 btm_right_corner_wgs84;
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

    Arena* arena_frame;
    Arena* arena_permanent;

    io::IO* io;
    ui::Camera* camera;
    dt_Time* time;
    city::Road* road;
    city::CarSim* car_sim;
    city::Buildings* buildings;
    cesium::TilesetRenderer* cesium_tileset;

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

g_internal Vec2F64
dt_default_bbox_size_meters_get();
g_internal Vec2F64
dt_default_tileset_wgs84_get();
g_internal Rng2F64
dt_wgs84_bbox_from_btm_right_corner(Vec2F64 btm_right_corner_wgs84, Vec2F64 bbox_size_meters);
g_internal Vec2F64
dt_utm_point_from_wgs84(Vec2F64 wgs84_point, char out_utm_zone[10]);
g_internal Rng2F64
dt_utm_from_wgs84(Rng2F64 wgs84_bbox);
g_internal Vec2F64
dt_wgs84_from_utm(Vec2F64 utm_point, const char* utm_zone);

static Buffer<String8>
dt_dir_create(Arena* arena, String8 parent, dt_DataDirPair* dirs, U32 count);

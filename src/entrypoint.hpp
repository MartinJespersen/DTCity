#pragma once

struct dt_Time
{
    F64 time_delta_constant_sec;
    F64 frame_timestamp_delta_ms;
    U64 frame_timestamp_ms;
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
    String8List cmdline;
    B32 running;
    String8 cwd;
    String8 data_dir;
    Buffer<String8> data_subdirs;

    Arena* arena_frame;
    Arena* arena;

    io::IO* io;
    dt_Time* time;
    ResourcePool<ui::Camera>* camera_container;
    ArrayResourcePool<cesium::TilesetRenderer>* tileset_pool;
    Pow2Freelist* pow2_freelist;

    cesium::TileRenderResources* tile_first;
    cesium::TileRenderResources* tile_last;
    U32 tile_count;
    city::TileLoadTaskStateNode* tile_load_task_first;
    city::TileLoadTaskStateNode* tile_load_task_last;
    city::TileLoadTaskStateNode* tile_load_task_free_list;
    ArrayResourcePool<city::Bvh>* polygon_bvh_pool;

    async::ThreadPool* thread_pool;
};

// ~mgj: Globals
static Context* g_ctx;
const U32 MAX_FONTS_IN_USE = 10;

// globals context
static void
dt_ctx_set(Context* ctx);
static Context*
dt_ctx_get();

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

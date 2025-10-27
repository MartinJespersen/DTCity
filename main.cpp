#include <cstdio>
#include <cstdlib>
#include <cstring>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan_core.h>

// user defined: [hpp]
#include "entrypoint.hpp"

// domain: cpp
#include "base/base_inc.cpp"
#include "os_core/os_core_inc.cpp"
#include "http/http_inc.cpp"
#include "async/async.cpp"
#include "lib_wrappers/lib_wrappers_inc.cpp"
#include "render/render_inc.cpp"
#include "ui/ui.cpp"
#include "osm/osm.cpp"
#include "city/city_inc.cpp"
#include "entrypoint.cpp"
#include "imgui/imgui_inc.cpp"

static Context*
ContextCreate(IO* io_ctx)
{
    HTTP_Init();

    ScratchScope scratch = ScratchScope(0, 0);
    //~mgj: app context setup
    Arena* app_arena = (Arena*)ArenaAlloc();
    Context* ctx = PushStruct(app_arena, Context);
    ctx->arena_permanent = app_arena;
    ctx->io = PushStruct(app_arena, IO);
    ctx->camera = PushStruct(app_arena, ui::Camera);
    ctx->time = PushStruct(app_arena, DT_Time);

    ctx->cwd = Str8PathFromStr8List(app_arena,
                                    {OS_GetCurrentPath(scratch.arena), S(".."), S(".."), S("..")});
    ctx->cache_path = Str8PathFromStr8List(app_arena, {ctx->cwd, S("cache")});
    ctx->texture_path = Str8PathFromStr8List(app_arena, {ctx->cwd, S("textures")});
    ctx->shader_path = Str8PathFromStr8List(app_arena, {ctx->cwd, S("shaders")});
    ctx->asset_path = Str8PathFromStr8List(app_arena, {ctx->cwd, S("assets")});
    ctx->io = io_ctx;

    // ~mgj: -2 as 2 are used for Main thread and IO thread
    U32 thread_count = OS_GetSystemInfo()->logical_processor_count - 2;
    U32 queue_size = 10; // TODO: should be increased
    ctx->thread_pool = async::WorkerThreadsCreate(app_arena, thread_count, queue_size);

    return ctx;
}

static void
ContextDestroy(Context* ctx)
{
    async::WorkerThreadDestroy(ctx->thread_pool);
    ArenaRelease(ctx->arena_permanent);
}

static G_Input
G_InterpretInput(int argc, char** argv)
{
    G_Input input = {};
    osm_GCSBoundingBox* bbox = &input.bbox;
    F64* bbox_coords[4] = {&bbox->lon_btm_left, &bbox->lat_btm_left, &bbox->lon_top_right,
                           &bbox->lat_top_right};

    if (argc != 1 && argc != 5)
    {
        ExitWithError("G_InterpretInput: Invalid number of arguments");
    }

    if (argc == 1)
    {
        DEBUG_LOG(
            "No command line arguments provided: Using default values\n"
            "lon_btm_left: %lf\n lat_btm_left: %lf\n lon_top_right: %lf\n lat_top_right: %lf\n",
            bbox->lon_btm_left, bbox->lat_btm_left, bbox->lon_top_right, bbox->lat_top_right);

        // Initialize default values
        bbox->lon_btm_left = 9.213970;
        bbox->lat_btm_left = 55.704686;
        bbox->lon_top_right = 9.22868;
        bbox->lat_top_right = 55.713671;
    }

    if (argc == 5)
    {
        for (U32 i = 0; (i < (U64)argc) && (i < ArrayCount(bbox_coords)); ++i)
        {
            char* arg = argv[i + 1];
            String8 arg_str = Str8CString(arg);
            F64 v = F64FromStr8(arg_str);
            *bbox_coords[i] = v;
        }
    }
    return input;
}

void
App(int argc, char** argv)
{
    ScratchScope scratch = ScratchScope(0, 0);

    IO* io_ctx = WindowCreate(VK_Context::WIDTH, VK_Context::HEIGHT);
    IO_InputStateUpdate(io_ctx);
    Context* ctx = ContextCreate(io_ctx);
    GlobalContextSet(ctx);
    TimeInit(ctx->time);
    G_Input input = G_InterpretInput(argc, argv);

    OS_Handle thread_handle = Entrypoint(ctx, &input);
    while (!glfwWindowShouldClose(ctx->io->window))
    {
        IO_InputStateUpdate(ctx->io);
    }
    Cleanup(thread_handle, ctx);
    ContextDestroy(ctx);
    WindowDestroy(io_ctx);
}

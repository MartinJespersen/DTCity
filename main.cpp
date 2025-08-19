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
#include "ui/ui.cpp"
#include "city/city_inc.cpp"
#include "entrypoint.cpp"

static Context*
ContextCreate()
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

    GlobalContextSet(ctx);
    InitWindow(ctx);

    // ~mgj: -2 as 2 are used for Main thread and IO thread
    U32 thread_count = OS_GetSystemInfo()->logical_processor_count - 2;
    U32 queue_size = 10; // TODO: should be increased
    ctx->thread_info = async::WorkerThreadsCreate(app_arena, thread_count, queue_size);

    ctx->vk_ctx = wrapper::VK_VulkanInit(ctx);
    ctx->road = city::RoadCreate(ctx->vk_ctx, ctx->cache_path);

    city::RoadsBuild(ctx->road);
    CameraInit(ctx->camera);
    ctx->car_sim = city::CarSimCreate(ctx->vk_ctx, 100, ctx->road);

    return ctx;
}

static void
ContextDestroy(Context* ctx)
{
    city::RoadDestroy(ctx->vk_ctx, ctx->road);
    city::CarSimDestroy(ctx->vk_ctx, ctx->car_sim);
    wrapper::VK_Cleanup(ctx, ctx->vk_ctx);

    glfwDestroyWindow(ctx->io->window);
    glfwTerminate();
    ArenaRelease(ctx->arena_permanent);
}

void
App()
{
    Context* ctx = ContextCreate();
    OS_Handle thread_handle = Entrypoint(ctx);
    while (!glfwWindowShouldClose(ctx->io->window))
    {
        IO_InputStateUpdate(ctx->io);
    }
    Cleanup(thread_handle, ctx);
    ContextDestroy(ctx);
}

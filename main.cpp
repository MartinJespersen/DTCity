#include <cstdio>
#include <cstdlib>
#include <cstring>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan_core.h>

// profiler
#include "profiler/tracy/Tracy.hpp"
#include "profiler/tracy/TracyVulkan.hpp"

// user defined: [hpp]
#include "base/base_inc.hpp"
#include "os_core/os_core_inc.hpp"
#include "http/http_inc.hpp"
#include "async/async.hpp"
#include "lib_wrappers/lib_wrappers_inc.hpp"
#include "ui/ui.hpp"
#include "city/city_inc.hpp"

// domain: cpp
#include "base/base_inc.cpp"
#include "os_core/os_core_inc.cpp"
#include "http/http_inc.cpp"
#include "async/async.cpp"
#include "lib_wrappers/lib_wrappers_inc.cpp"
#include "ui/ui.cpp"
#include "city/city_inc.cpp"

static void
ProfileBuffersCreate(wrapper::VulkanContext* vk_ctx, ProfilingContext* prof_ctx);
static void
ProfileBuffersCreate(wrapper::VulkanContext* vk_ctx, ProfilingContext* prof_ctx)
{
#ifdef TRACY_ENABLE
    prof_ctx->tracyContexts = BufferAlloc<TracyVkCtx>(vk_ctx->arena, vk_ctx->MAX_FRAMES_IN_FLIGHT);
    for (U32 i = 0; i < vk_ctx->command_buffers.size; i++)
    {
        prof_ctx->tracyContexts.data[i] =
            TracyVkContext(vk_ctx->physical_device, vk_ctx->device, vk_ctx->graphics_queue,
                           vk_ctx->command_buffers.data[i]);
    }
#endif
}

static Context*
ContextCreate()
{
    ScratchScope scratch = ScratchScope(0, 0);
    //~mgj: app context setup
    Arena* app_arena = (Arena*)ArenaAlloc();
    Context* ctx = PushStruct(app_arena, Context);
    ctx->arena_permanent = app_arena;
    ctx->io = PushStruct(app_arena, IO);
    ctx->camera = PushStruct(app_arena, ui::Camera);
    ctx->time = PushStruct(app_arena, DT_Time);
    ctx->profilingContext = PushStruct(app_arena, ProfilingContext);
    ctx->city = PushStruct(app_arena, city::City);
    ctx->dll_info = PushStruct(app_arena, DllInfo);

    ctx->cwd = Str8PathFromStr8List(app_arena,
                                    {OS_GetCurrentPath(scratch.arena), S(".."), S(".."), S("..")});

    GlobalContextSet(ctx);
    InitWindow(ctx);

    // ~mgj: -2 as 2 are used for Main thread and IO thread
    U32 thread_count = OS_GetSystemInfo()->logical_processor_count - 2;
    U32 queue_size = 10; // TODO: should be increased
    ctx->thread_info = async::WorkerThreadsCreate(app_arena, thread_count, queue_size);

    ctx->vk_ctx = wrapper::VK_VulkanInit(ctx);
    CameraInit(ctx->camera);
    ProfileBuffersCreate(ctx->vk_ctx, ctx->profilingContext);

    HTTP_Init();

    return ctx;
}

static void
ContextDestroy(Context* ctx)
{
    if (ctx->dll_info->entrypoint_thread_handle.u64[0])
    {
        ctx->dll_info->cleanup_func(ctx);
    }

    city::CityDestroy(ctx->city, ctx->vk_ctx);
    wrapper::VK_Cleanup(ctx->vk_ctx);

    glfwDestroyWindow(ctx->io->window);
    glfwTerminate();
}

void
App(HotReloadFunc HotReload)
{
    Context* ctx = ContextCreate();

    //~mgj: City Creation
    city::CityCreate(ctx, ctx->city);
    city::RoadsBuild(ctx->city->arena, ctx->city);
    ctx->city->w_road =
        wrapper::RoadCreate(ctx->thread_info->msg_queue, ctx->vk_ctx, &ctx->city->road);

    while (!glfwWindowShouldClose(ctx->io->window))
    {
        HotReload(ctx);
        IO_InputStateUpdate(ctx->io);
    }
    ContextDestroy(ctx);
}

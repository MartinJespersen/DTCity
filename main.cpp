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
#include "lib_wrappers/lib_wrappers_inc.hpp"
#include "ui/ui.hpp"
#include "city/city_inc.hpp"

// domain: cpp
#include "base/base_inc.cpp"
#include "os_core/os_core_inc.cpp"
#include "http/http_inc.cpp"
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
ContextInit()
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
    ctx->texture_path = Str8PathFromStr8List(app_arena, {ctx->cwd, S("textures")});

    GlobalContextSet(ctx);
    InitWindow(ctx);
    ctx->vk_ctx = wrapper::VK_VulkanInit(app_arena, ctx->io);
    CameraInit(ctx->camera);
    ProfileBuffersCreate(ctx->vk_ctx, ctx->profilingContext);
    city::CityInit(ctx->vk_ctx, ctx->city, ctx->cwd);

    HTTP_Init();

    return ctx;
}

void
App(HotReloadFunc HotReload)
{
    Context* ctx = ContextInit();
    city::RoadsBuild(ctx->city->arena, ctx->city);

    while (!glfwWindowShouldClose(ctx->io->window))
    {
        HotReload(ctx);
        IO_InputStateUpdate(ctx->io);
    }
    if (ctx->dll_info->entrypoint_thread_handle.u64[0])
    {
        ctx->dll_info->cleanup_func(ctx);
    }

    city::CityCleanup(ctx->city, ctx->vk_ctx);
    wrapper::VK_Cleanup(ctx->vk_ctx);

    glfwDestroyWindow(ctx->io->window);
    glfwTerminate();
}

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
    prof_ctx->tracyContexts =
        BufferAlloc<TracyVkCtx>(vk_ctx->arena, vk_ctx->swapchain_framebuffers.size);
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
    ctx->vk_ctx = PushStruct(app_arena, wrapper::VulkanContext);
    ctx->camera = PushStruct(app_arena, ui::Camera);
    ctx->time = PushStruct(app_arena, DT_Time);
    ctx->profilingContext = PushStruct(app_arena, ProfilingContext);
    ctx->terrain = PushStruct(app_arena, Terrain);
    ctx->city = PushStruct(app_arena, city::City);
    ctx->dll_info = PushStruct(app_arena, DllInfo);
    // TODO: find better solution for hardcoding the path
    // static Buffer<String8>
    // Str8BufferFromCString(Arena* arena, std::initializer_list<const char*> strings);
    // static String8
    // CreatePathFromStrings(Arena* arena, Buffer<String8> path_elements);

    ctx->cwd = Str8PathFromStr8List(app_arena, {OS_GetCurrentPath(scratch.arena), Str8CString(".."),
                                                Str8CString(".."), Str8CString("..")});

    GlobalContextSet(ctx);
    InitWindow(ctx);
    wrapper::VK_VulkanInit(ctx->vk_ctx, ctx->io);
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
    wrapper::VK_Cleanup();
}

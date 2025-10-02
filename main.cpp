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
    ctx->io = io_ctx;

    ctx->cwd = Str8PathFromStr8List(app_arena,
                                    {OS_GetCurrentPath(scratch.arena), S(".."), S(".."), S("..")});
    ctx->cache_path = Str8PathFromStr8List(app_arena, {ctx->cwd, S("cache")});

    GlobalContextSet(ctx);

    // ~mgj: -2 as 2 are used for Main thread and IO thread
    U32 thread_count = OS_GetSystemInfo()->logical_processor_count - 2;
    U32 queue_size = 10; // TODO: should be increased
    ctx->thread_info = async::WorkerThreadsCreate(app_arena, thread_count, queue_size);

    return ctx;
}

static void
ContextDestroy(Context* ctx)
{
    async::WorkerThreadDestroy(ctx->thread_info);
    ArenaRelease(ctx->arena_permanent);
}

void
App()
{
    IO* io_ctx = WindowCreate(wrapper::VulkanContext::WIDTH, wrapper::VulkanContext::HEIGHT);

    ImGui_ImplGlfw_InitForVulkan(io_ctx->window, true);
    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.ApiVersion =
        VK_API_VERSION_1_3; // Pass in your value of VkApplicationInfo::apiVersion, otherwise will
                            // default to header version.
    // init_info.Instance = g_Instance;
    // init_info.PhysicalDevice = g_PhysicalDevice;
    // init_info.Device = g_Device;
    // init_info.QueueFamily = g_QueueFamily;
    // init_info.Queue = g_Queue;
    // init_info.PipelineCache = g_PipelineCache;
    // init_info.DescriptorPool = g_DescriptorPool;
    // init_info.MinImageCount = g_MinImageCount;
    // init_info.ImageCount = wd->ImageCount;
    // init_info.Allocator = g_Allocator;
    // init_info.PipelineInfoMain.RenderPass = wd->RenderPass;
    // init_info.PipelineInfoMain.Subpass = 0;
    // init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    // init_info.CheckVkResultFn = check_vk_result;
    ImGui_ImplVulkan_Init(&init_info);

    IO_InputStateUpdate(io_ctx);
    Context* ctx = ContextCreate(io_ctx);
    TimeInit(ctx->time);
    OS_Handle thread_handle = Entrypoint(ctx);
    while (!glfwWindowShouldClose(ctx->io->window))
    {
        IO_InputStateUpdate(ctx->io);
    }
    Cleanup(thread_handle, ctx);
    ContextDestroy(ctx);
    WindowDestroy(io_ctx);
}

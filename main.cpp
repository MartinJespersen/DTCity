#include <cstdio>
#include <cstdlib>
#include <cstring>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan_core.h>

// profiler
#include "profiler/tracy/Tracy.hpp"
#include "profiler/tracy/TracyVulkan.hpp"

#include <windows.h>

// user defined: [hpp]
#include "base/base_inc.hpp"
#include "os_core/os_core_inc.h"
#include "http/http_inc.h"
#include "lib_wrappers/lib_wrappers_inc.hpp"
#include "ui/ui.hpp"
#include "city/city_inc.hpp"

// domain: cpp
#include "base/base_inc.cpp"
#include "os_core/os_core_inc.c"
#include "http/http_inc.c"
#include "lib_wrappers/lib_wrappers_inc.cpp"
#include "ui/ui.cpp"
#include "city/city_inc.cpp"

Context g_ctx_main;

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

static void
InitContext(Context* ctx)
{
    ctx->arena_permanent = (Arena*)ArenaAlloc();

    wrapper::VK_VulkanInit(ctx->vk_ctx, ctx->io);
    UI_CameraInit(ctx->camera);
    ProfileBuffersCreate(ctx->vk_ctx, ctx->profilingContext);
    city::CityInit(ctx->vk_ctx, ctx->city, Str8CString(g_ctx_main.cwd));
}

// Function to load or reload the DLL
static int
LoadDLL(DllInfo* dll_info)
{
    Temp scratch = ScratchBegin(0, 0);
    // Free the previous DLL if loaded
    if (dll_info->handle)
    {
        FreeLibrary(dll_info->handle);
    }

    if (!CopyFileA(dll_info->dll_temp_path, dll_info->dll_path, FALSE))
    {
        printf("Error: renaming DLL failed. Hotreload fail");
        return 0;
    };
    // Load the DLL
    // U8* dll_path_with_file_name = CombinePathAndFileName(scratch.arena, dll_info);
    dll_info->handle = LoadLibrary((char*)dll_info->dll_path);
    if (!dll_info->handle)
    {
        printf("Error loading DLL: %lu\n", GetLastError());
        return 0;
    }

    // Load the update function
    dll_info->func = (OS_Handle (*)(void*))GetProcAddress(dll_info->handle, dll_info->func_name);
    if (!dll_info->func)
    {
        printf("Error loading update function: %lu\n", GetLastError());
        FreeLibrary(dll_info->handle);
        dll_info->handle = NULL;
        return 0;
    }

    // Load the update function
    dll_info->cleanup_func =
        (void (*)(void*))GetProcAddress(dll_info->handle, dll_info->cleanup_func_name);
    if (!dll_info->func)
    {
        printf("Error loading update function: %lu\n", GetLastError());
        FreeLibrary(dll_info->handle);
        dll_info->handle = NULL;
        return 0;
    }

    // Get the DLL's last modified time
    HANDLE file = CreateFile((char*)dll_info->dll_path, GENERIC_READ, FILE_SHARE_READ, NULL,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
    {
        printf("Error opening DLL file: %lu\n", GetLastError());
        FreeLibrary(dll_info->handle);
        dll_info->handle = NULL;
        return 0;
    }
    GetFileTime(file, NULL, NULL, &dll_info->last_modified);
    CloseHandle(file);

    ScratchEnd(scratch);
    return 1;
}

void
run()
{
    w32_entry_point_caller(__argc, __wargv);

    HTTP_Init();

    wrapper::VulkanContext vk_ctx = {};
    ProfilingContext profilingContext = {};
    IO io_ctx = {};
    Terrain terrain = {};
    ui::Camera camera = {};
    DT_Time time = {};
    city::City city = {};

    DllInfo dll_info = {.func_name = "Entrypoint",
                        .cleanup_func_name = "Cleanup",
                        .dll_path = "build\\msc\\debug\\entrypoint.dll",
                        .dll_temp_path = "build\\msc\\debug\\entrypoint_temp.dll",
                        .handle = NULL,
                        .last_modified = 0,
                        .func = NULL};
    g_ctx_main = {0,       &dll_info,         {0},     &os_w32_state, 0,     &terrain,
                  &vk_ctx, &profilingContext, &io_ctx, &camera,       &time, &city};
    g_ctx_main.running = 1;

    InitWindow(&g_ctx_main);
    GlobalContextSet(&g_ctx_main);
    InitContext(&g_ctx_main);
    city::RoadsBuild(city.arena, &city);

    while (!glfwWindowShouldClose(io_ctx.window))
    {
        HANDLE file = CreateFile((char*)dll_info.dll_temp_path, GENERIC_READ, FILE_SHARE_READ, NULL,
                                 OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (file != INVALID_HANDLE_VALUE)
        {
            FILETIME current_modified;
            GetFileTime(file, NULL, NULL, &current_modified);
            CloseHandle(file);

            if (CompareFileTime(&current_modified, &dll_info.last_modified) > 0)
            {
                printf("DLL modified, reloading...\n");
                if (g_ctx_main.main_thread_handle.u64[0])
                {
                    g_ctx_main.dll_info->cleanup_func(&g_ctx_main);
                }

                if (LoadDLL(g_ctx_main.dll_info))
                {
                    g_ctx_main.running = true;
                    g_ctx_main.main_thread_handle = g_ctx_main.dll_info->func((void*)&g_ctx_main);
                    g_ctx_main.dll_info->last_modified = current_modified;
                }
                else
                {
                    printf("Failed to reload DLL\n");
                    Trap();
                }
            }
        }
        IO_InputStateUpdate(&io_ctx);
    }
    if (g_ctx_main.main_thread_handle.u64[0])
    {
        g_ctx_main.dll_info->cleanup_func(&g_ctx_main);
    }

    city::CityCleanup(&city, &vk_ctx);
    wrapper::VK_Cleanup();
}

// NOTE: Tracy profiler has a dlclose function and it takes precedence over the one in the standard
// library
//  This is only the case when -DTRACY_ENABLE is defined

#if BUILD_CONSOLE_INTERFACE
int
wmain(int argc, WCHAR** argv)
{
    run();
    return 0;
}
#else
int
wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nShowCmd)
{
    run();
    return 0;
}
#endif

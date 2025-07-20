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
    // TODO: find better solution for hardcoding the path
    ctx->cwd = Str8CString("C:\\repos\\DTCity");

    wrapper::VK_VulkanInit(ctx->vk_ctx, ctx->io);
    UI_CameraInit(ctx->camera);
    ProfileBuffersCreate(ctx->vk_ctx, ctx->profilingContext);
    city::CityInit(ctx->vk_ctx, ctx->city, ctx->cwd);
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
    ScratchScope scratch = ScratchScope(0, 0);
    HTTP_Init();

    Context* ctx = PushStruct(scratch.arena, Context);
    ctx->io = PushStruct(scratch.arena, IO);
    ctx->vk_ctx = PushStruct(scratch.arena, wrapper::VulkanContext);
    ctx->os_w32_state = &os_w32_state;
    ctx->camera = PushStruct(scratch.arena, ui::Camera);
    ctx->time = PushStruct(scratch.arena, DT_Time);
    ctx->profilingContext = PushStruct(scratch.arena, ProfilingContext);
    ctx->terrain = PushStruct(scratch.arena, Terrain);
    ctx->city = PushStruct(scratch.arena, city::City);
    ctx->dll_info = PushStruct(scratch.arena, DllInfo);

    DllInfo* dll_info = ctx->dll_info;
    dll_info->func_name = "Entrypoint";
    dll_info->cleanup_func_name = "Cleanup";
    dll_info->dll_path = "build\\msc\\debug\\entrypoint.dll";
    dll_info->dll_temp_path = "build\\msc\\debug\\entrypoint_temp.dll";
    dll_info->handle = NULL;
    dll_info->last_modified = {0};
    dll_info->func = NULL;

    ctx->running = 1;

    InitWindow(ctx);
    GlobalContextSet(ctx);
    InitContext(ctx);
    city::RoadsBuild(ctx->city->arena, ctx->city);

    while (!glfwWindowShouldClose(ctx->io->window))
    {
        HANDLE file = CreateFile((char*)ctx->dll_info->dll_temp_path, GENERIC_READ, FILE_SHARE_READ,
                                 NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (file != INVALID_HANDLE_VALUE)
        {
            FILETIME current_modified;
            GetFileTime(file, NULL, NULL, &current_modified);
            CloseHandle(file);

            if (CompareFileTime(&current_modified, &dll_info->last_modified) > 0)
            {
                printf("DLL modified, reloading...\n");
                if (ctx->main_thread_handle.u64[0])
                {
                    dll_info->cleanup_func(ctx);
                }

                if (LoadDLL(dll_info))
                {
                    ctx->running = true;
                    ctx->main_thread_handle = ctx->dll_info->func((void*)ctx);
                    ctx->dll_info->last_modified = current_modified;
                }
                else
                {
                    printf("Failed to reload DLL\n");
                    Trap();
                }
            }
        }
        IO_InputStateUpdate(ctx->io);
    }
    if (ctx->main_thread_handle.u64[0])
    {
        dll_info->cleanup_func(ctx);
    }

    city::CityCleanup(ctx->city, ctx->vk_ctx);
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

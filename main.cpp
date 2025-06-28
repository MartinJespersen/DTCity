#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/types.h>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan_core.h>

// user defined: [hpp]
#include "entrypoint.hpp"

// domain: cpp
#include "entrypoint.cpp"
// #include "base/base_inc.cpp"
// #include "ui/ui.cpp"

// profiler
#include "profiler/tracy/Tracy.hpp"
#include "profiler/tracy/TracyVulkan.hpp"

Context g_ctx_main;

void
run()
{
    VulkanContext vulkanContext = {};
    ProfilingContext profilingContext = {};
    UI_IO io_ctx = {};
    Terrain terrain = {};

    g_ctx_main = {0, 0, &terrain, &vulkanContext, &profilingContext, &io_ctx};

    w32_entry_point_caller(__argc, __wargv);

    g_ctx_main.running = 1;
    os_thread_launch(DrawFrame, (void*)&g_ctx_main, 0);

    InitWindow(&g_ctx_main);
    while (!glfwWindowShouldClose(io_ctx.window))
    {
        IO_InputStateUpdate(&io_ctx);
    }

    DeleteContext();
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

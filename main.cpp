#include <cstdlib>
#include <cstring>
#include <sys/types.h>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan_core.h>

// user defined: [hpp]
#include "entrypoint.hpp"

Context g_ctx_main;

void
run()
{
    ThreadCtx thread_ctx = {0};

    VulkanContext vulkanContext = {};
    ProfilingContext profilingContext = {};
    UI_IO input = {};
    Terrain terrain = {};

    g_ctx_main = {0, &terrain, &vulkanContext, &profilingContext, &input, &thread_ctx};

    GlobalContextSet(&g_ctx_main);
    InitContext();

    while (!glfwWindowShouldClose(vulkanContext.window))
    {
        glfwPollEvents();

        glfwGetCursorPos(vulkanContext.window, &input.mousePosition.x, &input.mousePosition.y);
        input.leftClicked =
            glfwGetMouseButton(vulkanContext.window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;

        drawFrame();
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
    w32_entry_point_caller(argc, argv);
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
// int
// main(int argc, char* argv[])
// {
//     w32_entry_point_caller(argc, argv);
//     run();
//     return EXIT_SUCCESS;
// }

#pragma once

// user defined: [hpp]
#include "base/base_inc.hpp"
#include "ui/ui.hpp"

const U32 MAX_FONTS_IN_USE = 10;

extern "C"
{
    void
    InitContext();
    void
    DeleteContext();
    void
    DrawFrame(void* ptr);
}

internal void
VK_Cleanup();

internal void
VulkanInit(VulkanContext* vk_ctx, UI_IO* io_ctx);

internal void
InitWindow(Context* ctx);

internal void
VK_FramebufferResizeCallback(GLFWwindow* window, int width, int height);

internal void
CommandBufferRecord(U32 imageIndex, U32 currentFrame);

internal void
ProfileBuffersCreate(VulkanContext* vk_ctx, ProfilingContext* prof_ctx);

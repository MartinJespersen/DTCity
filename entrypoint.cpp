
#include <cstdlib>
#include <cstring>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan_core.h>
// domain: hpp
#include "entrypoint.hpp"

// domain: cpp
#include "base/base_inc.cpp"
#include "ui/ui.cpp"

// profiler
#include "profiler/tracy/Tracy.hpp"
#include "profiler/tracy/TracyVulkan.hpp"

C_LINKAGE void
InitContext()
{
    Context* ctx = GlobalContextGet();
    ctx->arena_permanent = (Arena*)arena_alloc();

    ThreadContextInit();
    InitWindow();
    VulkanInit(ctx->vulkanContext);
    ProfileBuffersCreate(ctx->vulkanContext, ctx->profilingContext);
}

C_LINKAGE void
DeleteContext()
{
    VK_Cleanup();
    ThreadContextExit();
    Context* ctx = GlobalContextGet();
    ASSERT(ctx, "No Global Context found.");
}

internal void
VK_FramebufferResizeCallback(GLFWwindow* window, int width, int height)
{
    (void)width;
    (void)height;

    auto context = reinterpret_cast<Context*>(glfwGetWindowUserPointer(window));
    context->vulkanContext->framebuffer_resized = 1;
}

internal void
InitWindow()
{
    Context* ctx = GlobalContextGet();
    VulkanContext* vulkanContext = ctx->vulkanContext;

    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    vulkanContext->window = glfwCreateWindow(800, 600, "Vulkan", nullptr, nullptr);
    glfwSetWindowUserPointer(vulkanContext->window, ctx);
    glfwSetFramebufferSizeCallback(vulkanContext->window, VK_FramebufferResizeCallback);
}

internal void
VulkanInit(VulkanContext* vk_ctx)
{
    Temp scratch = scratch_begin(0, 0);

    vk_ctx->arena = arena_alloc();

    VK_CreateInstance(vk_ctx);
    VK_DebugMessengerSetup(vk_ctx);
    VK_SurfaceCreate(vk_ctx);
    VK_PhysicalDevicePick(vk_ctx);
    VK_LogicalDeviceCreate(scratch.arena, vk_ctx);
    SwapChainInfo swapChainInfo = VK_SwapChainCreate(scratch.arena, vk_ctx);
    U32 swapChainImageCount = VK_SwapChainImageCountGet(vk_ctx);
    vk_ctx->swapchain_images = BufferAlloc<VkImage>(vk_ctx->arena, (U64)swapChainImageCount);
    vk_ctx->swapchain_image_views =
        BufferAlloc<VkImageView>(vk_ctx->arena, (U64)swapChainImageCount);
    vk_ctx->swapchain_framebuffers =
        BufferAlloc<VkFramebuffer>(vk_ctx->arena, (U64)swapChainImageCount);

    VK_SwapChainImagesCreate(vk_ctx, swapChainInfo, swapChainImageCount);
    VK_SwapChainImageViewsCreate(vk_ctx);
    VK_CommandPoolCreate(vk_ctx);

    VK_ColorResourcesCreate(vk_ctx->physical_device, vk_ctx->device, vk_ctx->swapchain_image_format,
                            vk_ctx->swapchain_extent, vk_ctx->msaa_samples,
                            &vk_ctx->color_image_view, &vk_ctx->color_image,
                            &vk_ctx->color_image_memory);

    VK_DepthResourcesCreate(vk_ctx);

    VK_CommandBuffersCreate(vk_ctx);

    VK_SyncObjectsCreate(vk_ctx);
    VK_RenderPassCreate();

    TerrainInit();
    VK_FramebuffersCreate(vk_ctx, vk_ctx->vk_renderpass);
    scratch_end(scratch);
}

internal void
CommandBufferRecord(U32 image_index, U32 current_frame)
{
    ZoneScoped;
    Temp scratch = scratch_begin(0, 0);
    Context* ctx = GlobalContextGet();

    VulkanContext* vk_ctx = ctx->vulkanContext;
    ProfilingContext* profilingContext = ctx->profilingContext;
    (void)profilingContext;

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = 0;                  // Optional
    beginInfo.pInheritanceInfo = nullptr; // Optional

    if (vkBeginCommandBuffer(vk_ctx->command_buffers.data[current_frame], &beginInfo) != VK_SUCCESS)
    {
        exitWithError("failed to begin recording command buffer!");
    }

    TracyVkCollect(profilingContext->tracyContexts.data[current_frame],
                   vk_ctx->command_buffers.data[current_frame]);

    Buffer<Buffer<Vertex>> buf_of_vert_buffers = BufferAlloc<Buffer<Vertex>>(scratch.arena, 1);
    buf_of_vert_buffers.data[0] = ctx->terrain->vertices;

    Buffer<Buffer<U32>> buf_of_indice_buffers = BufferAlloc<Buffer<U32>>(scratch.arena, 1);
    buf_of_indice_buffers.data[0] = ctx->terrain->indices;

    VK_BufferContextCreate(vk_ctx, &vk_ctx->vk_vertex_context, buf_of_vert_buffers,
                           VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    VK_BufferContextCreate(vk_ctx, &vk_ctx->vk_indice_context, buf_of_indice_buffers,
                           VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    UpdateTerrainTransform(
        ctx->terrain,
        Vec2F32{(F32)vk_ctx->swapchain_extent.width, (F32)vk_ctx->swapchain_extent.height},
        current_frame);
    TerrainRenderPassBegin(vk_ctx, ctx->terrain, image_index, current_frame);

    if (vkEndCommandBuffer(vk_ctx->command_buffers.data[current_frame]) != VK_SUCCESS)
    {
        exitWithError("failed to record command buffer!");
    }
    scratch_end(scratch);
}

C_LINKAGE void
DrawFrame()
{
    ZoneScoped;
    Context* context = GlobalContextGet();
    VulkanContext* vulkanContext = context->vulkanContext;

    {
        ZoneScopedN("Wait for frame");
        vkWaitForFences(vulkanContext->device, 1,
                        &vulkanContext->in_flight_fences.data[vulkanContext->current_frame],
                        VK_TRUE, UINT64_MAX);
    }

    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(
        vulkanContext->device, vulkanContext->swapchain, UINT64_MAX,
        vulkanContext->image_available_semaphores.data[vulkanContext->current_frame],
        VK_NULL_HANDLE, &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
    {
        VK_RecreateSwapChain(vulkanContext);
        return;
    }
    else if (result != VK_SUCCESS)
    {
        exitWithError("failed to acquire swap chain image!");
    }

    vkResetFences(vulkanContext->device, 1,
                  &vulkanContext->in_flight_fences.data[vulkanContext->current_frame]);
    vkResetCommandBuffer(vulkanContext->command_buffers.data[vulkanContext->current_frame], 0);

    CommandBufferRecord(imageIndex, vulkanContext->current_frame);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    VkSemaphore waitSemaphores[] = {
        vulkanContext->image_available_semaphores.data[vulkanContext->current_frame]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &vulkanContext->command_buffers.data[vulkanContext->current_frame];

    VkSemaphore signalSemaphores[] = {
        vulkanContext->render_finished_semaphores.data[vulkanContext->current_frame]};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    if (vkQueueSubmit(vulkanContext->graphics_queue, 1, &submitInfo,
                      vulkanContext->in_flight_fences.data[vulkanContext->current_frame]) !=
        VK_SUCCESS)
    {
        exitWithError("failed to submit draw command buffer!");
    }

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;

    VkSwapchainKHR swapChains[] = {vulkanContext->swapchain};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &imageIndex;

    presentInfo.pResults = nullptr; // Optional

    result = vkQueuePresentKHR(vulkanContext->present_queue, &presentInfo);
    // TracyVkCollect(tracyContexts[currentFrame], commandBuffers[currentFrame]);
    FrameMark; // end of frame is assumed to be here
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR ||
        vulkanContext->framebuffer_resized)
    {
        vulkanContext->framebuffer_resized = 0;
        VK_RecreateSwapChain(vulkanContext);
    }
    else if (result != VK_SUCCESS)
    {
        exitWithError("failed to present swap chain image!");
    }

    vulkanContext->current_frame =
        (vulkanContext->current_frame + 1) % vulkanContext->MAX_FRAMES_IN_FLIGHT;
}

internal void
ProfileBuffersCreate(VulkanContext* vk_ctx, ProfilingContext* prof_ctx)
{
#ifdef PROFILING_ENABLE
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
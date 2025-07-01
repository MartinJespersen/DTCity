
#include <cstdlib>
#include <cstring>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan_core.h>
// domain: hpp
#include "entrypoint.hpp"

// // domain: cpp
#include "base/base_inc.cpp"
#include "ui/ui.cpp"

// // profiler
#include "profiler/tracy/Tracy.hpp"
#include "profiler/tracy/TracyVulkan.hpp"

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
    CameraUpdate(ctx);
    UpdateTerrainUniformBuffer(
        ctx->terrain, &ctx->view_matrix, &ctx->projection_matrix,
        Vec2F32{(F32)vk_ctx->swapchain_extent.width, (F32)vk_ctx->swapchain_extent.height},
        current_frame);
    TerrainRenderPassBegin(vk_ctx, ctx->terrain, image_index, current_frame);

    if (vkEndCommandBuffer(vk_ctx->command_buffers.data[current_frame]) != VK_SUCCESS)
    {
        exitWithError("failed to record command buffer!");
    }
    scratch_end(scratch);
}

shared_function OS_Handle
Entrypoint(void* ptr)
{
    Context* ctx = (Context*)ptr;
    GlobalContextSet(ctx);
    return os_thread_launch(MainLoop, ptr, NULL);
}

shared_function void
Cleanup(void* ptr)
{
    Context* ctx = (Context*)ptr;
    ctx->running = false;
    os_thread_join(ctx->main_thread_handle, max_U64);
    ctx->main_thread_handle.u64[0] = 0;
}

internal void
MainLoop(void* ptr)
{
    Context* ctx = (Context*)ptr;
    VulkanContext* vk_ctx = ctx->vulkanContext;
    os_set_thread_name(str8_cstring("Entrypoint thread"));

    while (ctx->running)
    {
        ZoneScoped;
        VulkanContext* vulkanContext = ctx->vulkanContext;
        IO* io_ctx = ctx->io;

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

        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR ||
            vulkanContext->framebuffer_resized)
        {
            vulkanContext->framebuffer_resized = false;
            VK_RecreateSwapChain(io_ctx, vulkanContext);
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
        submitInfo.pCommandBuffers =
            &vulkanContext->command_buffers.data[vulkanContext->current_frame];

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
            VK_RecreateSwapChain(io_ctx, vulkanContext);
        }
        else if (result != VK_SUCCESS)
        {
            exitWithError("failed to present swap chain image!");
        }

        vulkanContext->current_frame =
            (vulkanContext->current_frame + 1) % vulkanContext->MAX_FRAMES_IN_FLIGHT;
    }
    vkDeviceWaitIdle(vk_ctx->device);
}

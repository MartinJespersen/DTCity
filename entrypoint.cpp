
#include <cstdlib>
#include <cstring>

// // profiler
#include "profiler/tracy/Tracy.hpp"
#include "profiler/tracy/TracyVulkan.hpp"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan_core.h>
// domain: hpp
#include "entrypoint.hpp"

// // domain: cpp
#include "base/base_inc.cpp"
#include "os_core/os_core_inc.c"
#include "http/http_inc.c"
#include "lib_wrappers/lib_wrappers_inc.cpp"
#include "ui/ui.cpp"
#include "city/city_inc.cpp"

static void
DT_TimeInit(DT_Time* time)
{
    time->last_time_ms = os_now_microseconds();
}

static void
DT_UpdateTime(DT_Time* time)
{
    U64 cur_time = os_now_microseconds();
    time->delta_time_sec = (F32)(cur_time - time->last_time_ms) / 1'000'000.0;
    time->last_time_ms = cur_time;
}

static void
CommandBufferRecord(U32 image_index, U32 current_frame)
{
    ZoneScoped;
    Temp scratch = ScratchBegin(0, 0);
    Context* ctx = GlobalContextGet();

    wrapper::VulkanContext* vk_ctx = ctx->vk_ctx;
    ProfilingContext* profilingContext = ctx->profilingContext;
    ui::Camera* camera = ctx->camera;
    DT_Time* time = ctx->time;
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

    Buffer<Buffer<terrain::Vertex>> buf_of_vert_buffers =
        BufferAlloc<Buffer<terrain::Vertex>>(scratch.arena, 1);
    buf_of_vert_buffers.data[0] = ctx->terrain->vertices;

    Buffer<Buffer<U32>> buf_of_indice_buffers = BufferAlloc<Buffer<U32>>(scratch.arena, 1);
    buf_of_indice_buffers.data[0] = ctx->terrain->indices;

    wrapper::internal::VkBufferFromBuffers(vk_ctx->device, vk_ctx->physical_device,
                                           &vk_ctx->vk_vertex_context, buf_of_vert_buffers,
                                           VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    wrapper::internal::VkBufferFromBuffers(vk_ctx->device, vk_ctx->physical_device,
                                           &vk_ctx->vk_indice_context, buf_of_indice_buffers,
                                           VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

    // ~ transition swapchain image from undefined to color attachment optimal
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = vk_ctx->swapchain_images.data[image_index];
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    vkCmdPipelineBarrier(
        vk_ctx->command_buffers.data[current_frame], VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    DT_UpdateTime(time);
    UI_CameraUpdate(camera, ctx->io, ctx->time, vk_ctx->swapchain_extent);
    wrapper::internal::CameraUniformBufferUpdate(
        vk_ctx, camera,
        Vec2F32{(F32)vk_ctx->swapchain_extent.width, (F32)vk_ctx->swapchain_extent.height},
        current_frame);
    UpdateTerrainUniformBuffer(
        ctx->terrain,
        Vec2F32{(F32)vk_ctx->swapchain_extent.width, (F32)vk_ctx->swapchain_extent.height},
        current_frame);
    TerrainRenderPassBegin(vk_ctx, ctx->terrain, image_index, current_frame);
    city::CityUpdate(ctx->city, vk_ctx, image_index);

    // ~mgj: transition swapchain image layout from VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL to
    // VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
    barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = vk_ctx->swapchain_images.data[image_index];
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;

    vkCmdPipelineBarrier(
        vk_ctx->command_buffers.data[current_frame], VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    IO_InputReset(ctx->io);

    if (vkEndCommandBuffer(vk_ctx->command_buffers.data[current_frame]) != VK_SUCCESS)
    {
        exitWithError("failed to record command buffer!");
    }
    ScratchEnd(scratch);
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

static void
MainLoop(void* ptr)
{
    Context* ctx = (Context*)ptr;
    wrapper::VulkanContext* vk_ctx = ctx->vk_ctx;
    DT_Time* time = ctx->time;

    os_set_thread_name(Str8CString("Entrypoint thread"));
    DT_TimeInit(time);

    while (ctx->running)
    {
        ZoneScoped;
        wrapper::VulkanContext* vk_ctx = ctx->vk_ctx;
        IO* io_ctx = ctx->io;

        {
            ZoneScopedN("Wait for frame");
            vkWaitForFences(vk_ctx->device, 1,
                            &vk_ctx->in_flight_fences.data[vk_ctx->current_frame], VK_TRUE,
                            UINT64_MAX);
        }

        uint32_t imageIndex;
        VkResult result =
            vkAcquireNextImageKHR(vk_ctx->device, vk_ctx->swapchain, UINT64_MAX,
                                  vk_ctx->image_available_semaphores.data[vk_ctx->current_frame],
                                  VK_NULL_HANDLE, &imageIndex);

        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR ||
            vk_ctx->framebuffer_resized)
        {
            vk_ctx->framebuffer_resized = false;
            VK_RecreateSwapChain(io_ctx, vk_ctx);
            continue;
        }
        else if (result != VK_SUCCESS)
        {
            exitWithError("failed to acquire swap chain image!");
        }

        vkResetFences(vk_ctx->device, 1, &vk_ctx->in_flight_fences.data[vk_ctx->current_frame]);
        vkResetCommandBuffer(vk_ctx->command_buffers.data[vk_ctx->current_frame], 0);

        CommandBufferRecord(imageIndex, vk_ctx->current_frame);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        VkSemaphore waitSemaphores[] = {
            vk_ctx->image_available_semaphores.data[vk_ctx->current_frame]};
        VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = waitSemaphores;
        submitInfo.pWaitDstStageMask = waitStages;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &vk_ctx->command_buffers.data[vk_ctx->current_frame];

        VkSemaphore signalSemaphores[] = {
            vk_ctx->render_finished_semaphores.data[vk_ctx->current_frame]};
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = signalSemaphores;

        if (vkQueueSubmit(vk_ctx->graphics_queue, 1, &submitInfo,
                          vk_ctx->in_flight_fences.data[vk_ctx->current_frame]) != VK_SUCCESS)
        {
            exitWithError("failed to submit draw command buffer!");
        }

        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = signalSemaphores;

        VkSwapchainKHR swapChains[] = {vk_ctx->swapchain};
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = swapChains;
        presentInfo.pImageIndices = &imageIndex;

        presentInfo.pResults = nullptr; // Optional

        result = vkQueuePresentKHR(vk_ctx->present_queue, &presentInfo);
        // TracyVkCollect(tracyContexts[currentFrame], commandBuffers[currentFrame]);
        FrameMark; // end of frame is assumed to be here
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR ||
            vk_ctx->framebuffer_resized)
        {
            vk_ctx->framebuffer_resized = 0;
            VK_RecreateSwapChain(io_ctx, vk_ctx);
        }
        else if (result != VK_SUCCESS)
        {
            exitWithError("failed to present swap chain image!");
        }

        vk_ctx->current_frame = (vk_ctx->current_frame + 1) % vk_ctx->MAX_FRAMES_IN_FLIGHT;
    }
    vkDeviceWaitIdle(vk_ctx->device);
}

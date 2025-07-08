
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

    VulkanContext* vk_ctx = ctx->vulkanContext;
    ProfilingContext* profilingContext = ctx->profilingContext;
    UI_Camera* camera = ctx->camera;
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

    VK_BufferContextCreate(vk_ctx, &vk_ctx->vk_vertex_context, buf_of_vert_buffers,
                           VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    VK_BufferContextCreate(vk_ctx, &vk_ctx->vk_indice_context, buf_of_indice_buffers,
                           VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

    DT_UpdateTime(time);
    UI_CameraUpdate(camera, ctx->io, ctx->time, vk_ctx->swapchain_extent);
    UpdateTerrainUniformBuffer(
        ctx->terrain, camera,
        Vec2F32{(F32)vk_ctx->swapchain_extent.width, (F32)vk_ctx->swapchain_extent.height},
        current_frame);
    TerrainRenderPassBegin(vk_ctx, ctx->terrain, image_index, current_frame);
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
    VulkanContext* vk_ctx = ctx->vulkanContext;
    DT_Time* time = ctx->time;

    os_set_thread_name(Str8CString("Entrypoint thread"));
    DT_TimeInit(time);

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
            continue;
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

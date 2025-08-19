
#include <cstdlib>
#include <cstring>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan_core.h>

// domain: hpp
#include "entrypoint.hpp"

// // domain: cpp
#include "base/base_inc.cpp"
#include "os_core/os_core_inc.cpp"
#include "async/async.cpp"
#include "http/http_inc.cpp"
#include "lib_wrappers/lib_wrappers_inc.cpp"
#include "ui/ui.cpp"
#include "city/city_inc.cpp"

//~ mgj: Entrypoint Stub for application. Necessary as this layer includes the os layer but does not
// contain the entrypoint in a hot reloading scenario.
// TODO: Find a better way maybe.
void App(HotReloadFunc){};

static void
ProfileBuffersCreate(wrapper::VulkanContext* vk_ctx)
{
#ifdef TRACY_ENABLE
    for (U32 i = 0; i < ArrayCount(vk_ctx->tracy_ctx); i++)
    {
        vk_ctx->tracy_ctx[i] =
            TracyVkContext(vk_ctx->physical_device, vk_ctx->device, vk_ctx->graphics_queue,
                           vk_ctx->command_buffers.data[i]);
    }
#endif
}

static void
ProfileBuffersDestroy(wrapper::VulkanContext* vk_ctx)
{
#ifdef TRACY_ENABLE
    vkQueueWaitIdle(vk_ctx->graphics_queue);
    for (U32 i = 0; i < ArrayCount(vk_ctx->tracy_ctx); i++)
    {
        TracyVkDestroy(vk_ctx->tracy_ctx[i]);
    }
#endif
}
static void
TimeInit(DT_Time* time)
{
    time->last_time_ms = os_now_microseconds();
}

static void
UpdateTime(DT_Time* time)
{
    U64 cur_time = os_now_microseconds();
    time->delta_time_sec = (F32)(cur_time - time->last_time_ms) / 1'000'000.0;
    time->last_time_ms = cur_time;
}

static void
CommandBufferRecord(U32 image_index, U32 current_frame)
{
    ProfScopeMarker;
    Temp scratch = ScratchBegin(0, 0);
    Context* ctx = GlobalContextGet();

    wrapper::VulkanContext* vk_ctx = ctx->vk_ctx;
    ui::Camera* camera = ctx->camera;
    DT_Time* time = ctx->time;

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = 0;                  // Optional
    beginInfo.pInheritanceInfo = nullptr; // Optional

    // ~mgj: Render scope (Tracy Profiler documentation says this is necessary)
    {
        VkCommandBuffer current_cmd_buf = vk_ctx->command_buffers.data[current_frame];
        vkResetCommandBuffer(current_cmd_buf, 0);
        VkResult result_vk = vkBeginCommandBuffer(current_cmd_buf, &beginInfo);
        if (result_vk)
        {
            exitWithError("failed to begin recording command buffer!");
        }

        // ~mgj: this scope is necessary to avoid the vulkan validation error:
        // validation layer: vkDestroyQueryPool(): can't be called on VkQueryPool 0x9638f80000000036
        // that is currently in use by VkCommandBuffer 0x121e6955ed50.
        {
            TracyVkZone(vk_ctx->tracy_ctx[current_frame], current_cmd_buf, "Render");

            wrapper::SwapchainResources* swapchain_resource = vk_ctx->swapchain_resources;
            VkImage color_image = swapchain_resource->color_image_resource.image_alloc.image;
            VkImage depth_image = swapchain_resource->depth_image_resource.image_alloc.image;
            VkImage swapchain_image = swapchain_resource->image_resources.data[image_index].image;
            VkClearColorValue clear_color = {0.0f, 0.0f, 0.0f, 1.0f};
            VkClearDepthStencilValue clear_depth = {1.0f, 0};
            VkImageSubresourceRange image_range = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                                   .baseMipLevel = 0,
                                                   .levelCount = 1,
                                                   .baseArrayLayer = 0,
                                                   .layerCount = 1};

            wrapper::ClearDepthAndColorImage(current_cmd_buf, color_image, depth_image, clear_color,
                                             clear_depth);
            // ~ transition swapchain image from undefined to color attachment optimal
            VkImageMemoryBarrier2 barrier_before{};
            barrier_before.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            barrier_before.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier_before.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            barrier_before.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier_before.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier_before.image = swapchain_image;
            barrier_before.srcAccessMask = VK_ACCESS_2_NONE;
            barrier_before.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            barrier_before.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
            barrier_before.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            barrier_before.subresourceRange = image_range;

            VkDependencyInfo render_to_info = {
                .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .imageMemoryBarrierCount = 1,
                .pImageMemoryBarriers = &barrier_before,
            };

            vkCmdPipelineBarrier2(current_cmd_buf, &render_to_info);

            UpdateTime(time);
            CameraUpdate(camera, ctx->io, ctx->time, vk_ctx->swapchain_resources->swapchain_extent);
            wrapper::CameraUniformBufferUpdate(
                vk_ctx, camera,
                Vec2F32{(F32)vk_ctx->swapchain_resources->swapchain_extent.width,
                        (F32)vk_ctx->swapchain_resources->swapchain_extent.height},
                current_frame);
            wrapper::RoadUpdate(ctx->road, vk_ctx, image_index, vk_ctx->shader_path);

            Buffer<city::CarInstance> instance_buffer =
                city::CarUpdate(scratch.arena, ctx->car_sim, ctx->road, ctx->time->delta_time_sec);
            wrapper::CarUpdate(ctx->car_sim, instance_buffer, image_index);

            // ~mgj: transition swapchain image for presentation
            VkImageMemoryBarrier2 barrier_after = {};
            barrier_after.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            barrier_after.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            barrier_after.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            barrier_after.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier_after.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier_after.image =
                vk_ctx->swapchain_resources->image_resources.data[image_index].image;
            barrier_after.subresourceRange = image_range;
            barrier_after.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            barrier_after.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT;
            barrier_after.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            barrier_after.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;

            VkDependencyInfo render_after_info = {
                .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .imageMemoryBarrierCount = 1,
                .pImageMemoryBarriers = &barrier_after,
            };

            vkCmdPipelineBarrier2(current_cmd_buf, &render_after_info);

            IO_InputReset(ctx->io);
        }
        TracyVkCollect(vk_ctx->tracy_ctx[current_frame], current_cmd_buf);
        VkResult result = vkEndCommandBuffer(current_cmd_buf);
        if (result)
        {
            exitWithError("failed to record command buffer!");
        }
    }
    ScratchEnd(scratch);
}

shared_function OS_Handle
Entrypoint(void* ptr)
{
    Context* ctx = (Context*)ptr;
    ThreadCtxSet(ctx);
    OS_GlobalStateSetFromPtr(ctx->os_state);

    return OS_ThreadLaunch(MainLoop, ptr, NULL);
}

shared_function void
Cleanup(void* ptr)
{
    Context* ctx = (Context*)ptr;

    ctx->running = false;
    OS_ThreadJoin(ctx->dll_info->entrypoint_thread_handle, max_U64);
    ctx->dll_info->entrypoint_thread_handle.u64[0] = 0;
}

static void
MainLoop(void* ptr)
{
    Context* ctx = (Context*)ptr;
    wrapper::VulkanContext* vk_ctx = ctx->vk_ctx;
    DT_Time* time = ctx->time;
    IO* io_ctx = ctx->io;
    OS_SetThreadName(Str8CString("Entrypoint thread"));

    TimeInit(time);

    ProfileBuffersCreate(vk_ctx);
    while (ctx->running)
    {
        wrapper::AssetManagerExecuteCmds();
        wrapper::AssetManagerCmdDoneCheck();

        {
            ProfScopeMarkerNamed("Wait for frame");
            VK_CHECK_RESULT(vkWaitForFences(vk_ctx->device, 1,
                                            &vk_ctx->in_flight_fences.data[vk_ctx->current_frame],
                                            VK_TRUE, UINT64_MAX));
        }

        uint32_t imageIndex;
        VkResult result = vkAcquireNextImageKHR(
            vk_ctx->device, vk_ctx->swapchain_resources->swapchain, UINT64_MAX,
            vk_ctx->image_available_semaphores.data[vk_ctx->current_frame], VK_NULL_HANDLE,
            &imageIndex);

        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR ||
            vk_ctx->framebuffer_resized)
        {
            vk_ctx->framebuffer_resized = false;
            VK_RecreateSwapChain(io_ctx, vk_ctx);
            vk_ctx->current_frame = (vk_ctx->current_frame + 1) % vk_ctx->MAX_FRAMES_IN_FLIGHT;
            continue;
        }
        else if (result != VK_SUCCESS)
        {
            exitWithError("failed to acquire swap chain image!");
        }

        vkResetFences(vk_ctx->device, 1, &vk_ctx->in_flight_fences.data[vk_ctx->current_frame]);

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

        vkQueueSubmit(vk_ctx->graphics_queue, 1, &submitInfo,
                      vk_ctx->in_flight_fences.data[vk_ctx->current_frame]);

        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = signalSemaphores;

        VkSwapchainKHR swapChains[] = {vk_ctx->swapchain_resources->swapchain};
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = swapChains;
        presentInfo.pImageIndices = &imageIndex;

        presentInfo.pResults = nullptr; // Optional

        result = vkQueuePresentKHR(vk_ctx->present_queue, &presentInfo);
        ProfFrameMarker; // end of frame is assumed to be here
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
    ProfileBuffersDestroy(vk_ctx);
    vkDeviceWaitIdle(vk_ctx->device);
}

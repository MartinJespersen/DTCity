
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
    return OS_ThreadLaunch(MainLoop, ptr, NULL);
}

shared_function void
Cleanup(void* ptr)
{
    Context* ctx = (Context*)ptr;
    ctx->running = false;
    os_thread_join(ctx->main_thread_handle, max_U64);
    ctx->main_thread_handle.u64[0] = 0;
}

struct Queue;
struct ThreadInfo
{
    Queue* queue;
    U32 thread_id;
};
typedef void (*WorkerFunc)(ThreadInfo, void*);

struct QueueItem
{
    void* data;
    WorkerFunc worker_func;
};

struct Queue
{
    volatile U32 next_index;
    volatile U32 fill_index;
    U32 queue_size;
    QueueItem* items;
    OS_Handle mutex;
    OS_Handle semaphore_empty;
    OS_Handle semaphore_full;
};

struct ThreadInput
{
    Queue* queue;

    U32 thread_count;
    U32 thread_id;
};

Queue*
QueueInit(Arena* arena, U32 queue_size, U32 thread_count)
{
    //~mgj: Semaphore size count in the full state is 1 less than the queue size so that assert
    // works in thread worker when the queue is full and full_index is equal to next_index
    U32 semaphore_full_size = queue_size - 1;
    Queue* queue = PushStruct(arena, Queue);
    queue->queue_size = queue_size;
    queue->items = PushArray(arena, QueueItem, queue_size);
    queue->mutex = OS_RWMutexAlloc();
    queue->semaphore_empty =
        OS_SemaphoreAlloc(0, thread_count, Str8CString("queue_empty_semaphore"));
    queue->semaphore_full = OS_SemaphoreAlloc(semaphore_full_size, semaphore_full_size,
                                              Str8CString("queue_full_semaphore"));
    return queue;
}

void
QueueDestroy(Queue* queue)
{
    OS_MutexRelease(queue->mutex);
    OS_SemaphoreRelease(queue->semaphore_empty);
}

void
PushToQueue(Queue* queue, void* data, WorkerFunc worker_func)
{
    QueueItem* item;
    U32 fill_index;
    OS_SemaphoreTake(queue->semaphore_full, max_U64);
    OS_MutexScopeW(queue->mutex)
    {
        fill_index = queue->fill_index;
        queue->fill_index = (fill_index + 1) % queue->queue_size;
        Assert(queue->fill_index != queue->next_index);
    }
    OS_SemaphoreDrop(queue->semaphore_empty);
    item = &queue->items[fill_index];
    item->data = data;
    item->worker_func = worker_func;
}

void
ThreadWorker(void* data)
{
    ThreadInput* input = (ThreadInput*)data;
    Queue* queue = input->queue;
    U32 thread_count = input->thread_count;
    U32 thread_id = input->thread_id;

    ThreadInfo thread_info;
    thread_info.thread_id = thread_id;
    thread_info.queue = queue;

    QueueItem item;
    U32 cur_index;
    B32 is_waiting = 0;
    while (true)
    {
        OS_MutexScopeW(queue->mutex)
        {
            cur_index = queue->next_index;
            if (cur_index != queue->fill_index)
            {
                queue->next_index = (cur_index + 1) % queue->queue_size;
            }
            else
            {
                is_waiting = 1;
            }
        }
        if (is_waiting)
        {
            OS_SemaphoreTake(queue->semaphore_empty, max_U64);
            is_waiting = 0;
        }
        else
        {
            OS_SemaphoreDrop(queue->semaphore_full);
            QueueItem* item = &queue->items[cur_index];
            item->worker_func(thread_info, item->data);
        }
    }
}

void
TestWorker(ThreadInfo thread_info, void* data)
{
    ScratchScope scratch = ScratchScope(0, 0);
    char* message = (char*)data;
    String8 str =
        PushStr8F(scratch.arena, (char*)"%s threadid: %d", message, thread_info.thread_id);
    printf("%s\n", str.str);
    fflush(stdout);
}

static void
MainLoop(void* ptr)
{
    Context* ctx = (Context*)ptr;
    wrapper::VulkanContext* vk_ctx = ctx->vk_ctx;
    DT_Time* time = ctx->time;
    os_set_thread_name(Str8CString("Entrypoint thread"));

    ScratchScope scratch = ScratchScope(0, 0);

    // U32 test_push_count = 100;
    U32 queue_size = 30;
    U32 processor_count = ctx->os_w32_state->system_info.logical_processor_count;
    Queue* queue = QueueInit(ctx->arena_permanent, queue_size, processor_count);
    ThreadInput* thread_inputs = PushArray(ctx->arena_permanent, ThreadInput, processor_count);
    for (U32 i = 0; i < processor_count; i++)
    {
        thread_inputs[i].thread_count = processor_count;
        thread_inputs[i].thread_id = i;
        thread_inputs[i].queue = queue;

        OS_ThreadLaunch(ThreadWorker, &thread_inputs[i], NULL);
    }

    for (U32 i = 0; i < 10; i++)
    {
        String8 str = PushStr8F(scratch.arena, (char*)"Count: %d, ", i);
        PushToQueue(queue, (void*)str.str, TestWorker);
    }
    Sleep(5000);
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

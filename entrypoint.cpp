
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
DT_TimeInit(DT_Time* time)
{
    time->last_time_ms = os_now_microseconds();
}
#define CGLTF_IMPLEMENTATION
#include "third_party/cgltf.h"

#define KHRONOS_STATIC
#include "third_party/ktx/include/ktx.h"

static wrapper::Car*
CarPrepare(wrapper::VulkanContext* vk_ctx)
{
    ScratchScope scratch = ScratchScope(0, 0);
    // test loading car
    Arena* arena = ArenaAlloc();
    const char* gltf_path = "../../../assets/cars/scene.gltf";
    Buffer<wrapper::CarVertex> vertex_buffer;
    Buffer<U32> index_buffer;

    cgltf_options options = {0};
    cgltf_data* data = NULL;

    cgltf_accessor* accessor_position = NULL;
    cgltf_accessor* accessor_uv = NULL;

    cgltf_result result = cgltf_parse_file(&options, gltf_path, &data);
    if (result == cgltf_result_success)
    {
        result = cgltf_load_buffers(&options, data, gltf_path);
        if (result != cgltf_result_success)
        {
            // handle error
            exitWithError("failed to load buffer");
        }
        cgltf_mesh* mesh = &data->meshes[0];
        for (U32 prim_idx = 0; prim_idx < mesh->primitives_count; ++prim_idx)
        {
            cgltf_primitive* primitive = &mesh->primitives[prim_idx];

            U32 expected_count = primitive->attributes[0].data->count;
            for (size_t i = 1; i < primitive->attributes_count; ++i)
            {
                if (primitive->attributes[i].data->count != expected_count)
                {
                    fprintf(stderr,
                            "Non-shared indexing detected: attribute %zu has %zu entries (expected "
                            "%zu)\n",
                            i, primitive->attributes[i].data->count, expected_count);
                    exit(1); // or handle with deduplication logic
                }
            }

            for (size_t i = 0; i < primitive->attributes_count; ++i)
            {
                cgltf_attribute* attr = &primitive->attributes[i];

                switch (attr->type)
                {
                case cgltf_attribute_type_position:
                    accessor_position = attr->data;
                    break;
                case cgltf_attribute_type_texcoord:
                    if (attr->index == 0) // TEXCOORD_0
                        accessor_uv = attr->data;
                    break;
                default:
                    break;
                }
            }

            cgltf_accessor* accessor_indices = primitive->indices;
            index_buffer = BufferAlloc<U32>(arena, accessor_indices->count);
            for (U32 indice_idx = 0; indice_idx < accessor_indices->count; indice_idx++)
            {
                U32 index = cgltf_accessor_read_index(accessor_indices, indice_idx);
                index_buffer.data[indice_idx] = index;
            }

            vertex_buffer = BufferAlloc<wrapper::CarVertex>(arena, expected_count);
            for (U32 vertex_idx = 0; vertex_idx < vertex_buffer.size; vertex_idx++)
            {
                if (accessor_position)
                    cgltf_accessor_read_float(accessor_position, vertex_idx,
                                              vertex_buffer.data[vertex_idx].position, 3);
                if (accessor_uv)
                    cgltf_accessor_read_float(accessor_uv, vertex_idx,
                                              vertex_buffer.data[vertex_idx].uv, 2);
            }
        }

        cgltf_free(data);
    }

    VkFenceCreateInfo fence_info = {};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

    VkFence vk_fence;
    vkCreateFence(vk_ctx->device, &fence_info, nullptr, &vk_fence);
    VkCommandPoolCreateInfo cmd_pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = vk_ctx->queue_family_indices.graphicsFamilyIndex,
    };
    VkCommandPool cmd_pool;
    vkCreateCommandPool(vk_ctx->device, &cmd_pool_info, nullptr, &cmd_pool);
    wrapper::BufferAllocation vertex_buffer_allocation = BufferUploadDevice(
        vk_fence, cmd_pool, vk_ctx, vertex_buffer, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    wrapper::BufferAllocation index_buffer_allocation = BufferUploadDevice(
        vk_fence, cmd_pool, vk_ctx, index_buffer, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

    // Texture loading
    ktxTexture2* texture;
    ktx_uint32_t level, layer, faceSlice;

    ktx_error_code_e ktxresult = ktxTexture2_CreateFromNamedFile(
        "../../../textures/car_collection.ktx", KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &texture);
    Assert(ktxresult == KTX_SUCCESS);
    U32 img_width = texture->baseWidth;
    U32 img_height = texture->baseHeight;
    VkDeviceSize img_size = texture->dataSize;
    VkFormat vk_format = (VkFormat)texture->vkFormat;
    U32 mip_levels = texture->numLevels;

    VmaAllocationCreateInfo vma_staging_info = {0};
    vma_staging_info.usage = VMA_MEMORY_USAGE_AUTO;
    vma_staging_info.flags =
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    vma_staging_info.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;

    wrapper::BufferAllocation texture_staging_buffer = wrapper::BufferAllocationCreate(
        vk_ctx->allocator, img_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, vma_staging_info);

    vmaCopyMemoryToAllocation(vk_ctx->allocator, texture->pData, texture_staging_buffer.allocation,
                              0, img_size);

    VmaAllocationCreateInfo vma_info = {.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};

    wrapper::ImageAllocation image_alloc = wrapper::ImageAllocationCreate(
        vk_ctx->allocator, img_width, img_height, VK_SAMPLE_COUNT_1_BIT, VK_FORMAT_R8G8B8A8_SRGB,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
            VK_IMAGE_USAGE_SAMPLED_BIT,
        mip_levels, vma_info);

    VkCommandPoolCreateInfo cmd_pool_create_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = vk_ctx->queue_family_indices.graphicsFamilyIndex,

    };

    VkBufferImageCopy* regions = PushArray(scratch.arena, VkBufferImageCopy, mip_levels);

    for (uint32_t level = 0; level < mip_levels; ++level)
    {
        ktx_size_t offset;
        ktxTexture_GetImageOffset((ktxTexture*)texture, level, 0, 0, &offset);

        uint32_t mip_width = texture->baseWidth >> level;
        uint32_t mip_height = texture->baseHeight >> level;
        uint32_t mip_depth = texture->baseDepth >> level;

        if (mip_width == 0)
            mip_width = 1;
        if (mip_height == 0)
            mip_height = 1;
        if (mip_depth == 0)
            mip_depth = 1;

        regions[level] = {.bufferOffset = offset,
                          .bufferRowLength = 0, // tightly packed
                          .bufferImageHeight = 0,
                          .imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                               .mipLevel = level,
                                               .baseArrayLayer = 0,
                                               .layerCount = 1},
                          .imageOffset = {0, 0, 0},
                          .imageExtent = {mip_width, mip_height, mip_depth}};
    }

    VkCommandBuffer cmd =
        wrapper::VK_BeginSingleTimeCommands(vk_ctx->device, cmd_pool); // Your helper

    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image_alloc.image,
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                             .baseMipLevel = 0,
                             .levelCount = mip_levels,
                             .baseArrayLayer = 0,
                             .layerCount = 1},
    };

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                         0, NULL, 0, NULL, 1, &barrier);

    vkCmdCopyBufferToImage(cmd, texture_staging_buffer.buffer, image_alloc.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mip_levels, regions);

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, NULL, 0, NULL, 1, &barrier);
    VkFence img_fence;
    VkFenceCreateInfo fence_create_info = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    vkCreateFence(vk_ctx->device, &fence_create_info, NULL, &img_fence);
    wrapper::VK_EndSingleTimeCommands(vk_ctx, cmd_pool, cmd, img_fence);

    // create texture
    wrapper::ImageViewResource image_view_resource =
        wrapper::ImageViewResourceCreate(vk_ctx->device, image_alloc.image, VK_FORMAT_R8G8B8A8_SRGB,
                                         VK_IMAGE_ASPECT_COLOR_BIT, mip_levels);

    VkSampler sampler = wrapper::SamplerCreate(
        vk_ctx->device, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR, mip_levels,
        vk_ctx->physical_device_properties.limits.maxSamplerAnisotropy);

    wrapper::ImageResource image_resource = {.image_alloc = image_alloc,
                                             .image_view_resource = image_view_resource};

    wrapper::Texture* car_texture = PushStruct(vk_ctx->arena, wrapper::Texture);
    car_texture->image_resource = image_resource;
    car_texture->sampler = sampler;
    car_texture->height = img_height;
    car_texture->width = img_width;
    car_texture->mip_level_count = mip_levels;

    // Create descriptors
    VkDescriptorSetLayoutBinding desc_set_layout_info = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
    };
    VkDescriptorSetLayout desc_set_layout =
        wrapper::DescriptorSetLayoutCreate(vk_ctx->device, &desc_set_layout_info, 1);
    Buffer<VkDescriptorSet> desc_sets =
        wrapper::DescriptorSetCreate(vk_ctx->arena, vk_ctx->device, vk_ctx->descriptor_pool,
                                     desc_set_layout, car_texture, vk_ctx->MAX_FRAMES_IN_FLIGHT);

    // vertex buffer input
    wrapper::PipelineInfo pipeline_info = wrapper::CarPipelineCreate(vk_ctx, desc_set_layout);
    wrapper::BufferInfo<wrapper::CarVertex> vertex_buffer_info = {
        .buffer_alloc = vertex_buffer_allocation, .buffer = vertex_buffer};
    wrapper::BufferInfo<U32> index_buffer_info = {.buffer_alloc = index_buffer_allocation,
                                                  .buffer = index_buffer};
    wrapper::Car* car = PushStruct(vk_ctx->arena, wrapper::Car);
    car->pipeline_info = pipeline_info;
    car->index_buffer_info = index_buffer_info;
    car->vertex_buffer_info = vertex_buffer_info;
    car->texture = car_texture;
    car->descriptor_set_layout = desc_set_layout;
    car->descriptor_sets = desc_sets;

    return car;
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

    // ~ transition swapchain image from undefined to color attachment optimal
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = vk_ctx->swapchain_resources->image_resources.data[image_index].image;
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
    CameraUpdate(camera, ctx->io, ctx->time, vk_ctx->swapchain_resources->swapchain_extent);
    wrapper::CameraUniformBufferUpdate(
        vk_ctx, camera,
        Vec2F32{(F32)vk_ctx->swapchain_resources->swapchain_extent.width,
                (F32)vk_ctx->swapchain_resources->swapchain_extent.height},
        current_frame);
    city::CityUpdate(ctx->city, vk_ctx, image_index, vk_ctx->shader_path);

    // Cars Update
    // NOTE: The matrix is row major on cpu but column major on gpu. This changes rotation from
    // counter-clockise to clockwise. The translation is therefore in the last row.
    wrapper::CarInstance car_instance = {{1.0f, 0.0f, 0.0f, 0.0f},
                                         {0.0f, 0.0f, -1.0f, 0.0f},
                                         {0.0f, 1.0f, 0.0f, 0.0f},
                                         {0.0f, 10.0f, 0.0f, 1.0f}};
    wrapper::CarInstance car_instance_1 = {{1.0f, 0.0f, 0.0f, 0.0f},
                                           {0.0f, 0.0f, -1.0f, 0.0f},
                                           {0.0f, 1.0f, 0.0f, 0.0f},
                                           {0.0f, 10.0f, 10.0f, 1.0f}};

    Buffer<wrapper::CarInstance>* cars = &vk_ctx->car->car_instances;
    *cars = BufferAlloc<wrapper::CarInstance>(scratch.arena, 2);
    cars->data[0] = car_instance;
    cars->data[1] = car_instance_1;

    VkBufferFromBufferMapping(vk_ctx->allocator, &vk_ctx->car->instance_buffer_mapped,
                              vk_ctx->car->car_instances, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

    CarRendering(vk_ctx, vk_ctx->car, image_index);
    // ~mgj: transition swapchain image layout from VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL to
    // VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
    barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = vk_ctx->swapchain_resources->image_resources.data[image_index].image;
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
    OS_SetThreadName(Str8CString("Entrypoint thread"));
    vk_ctx->car = CarPrepare(vk_ctx);

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

        wrapper::ThreadedGraphicsQueueSubmit(vk_ctx->graphics_queue, &submitInfo,
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

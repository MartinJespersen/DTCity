namespace render
{

static void
null_texture_create(vulkan::Context* vk_ctx)
{
    render::SamplerInfo sampler_info = {
        .min_filter = render::Filter_Nearest,
        .mag_filter = render::Filter_Nearest,
        .mip_map_mode = render::MipMapMode_Nearest,
        .address_mode_u = render::SamplerAddressMode_Repeat,
        .address_mode_v = render::SamplerAddressMode_Repeat,
    };
    // Create a 1x1 RGBA8 image
    VmaAllocationCreateInfo vma_info = {.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
    vulkan::ImageAllocation image_alloc = vulkan::image_allocation_create(1, 1, VK_SAMPLE_COUNT_1_BIT, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL,
                                                                          VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, 1, vma_info, "null_texture image");

    vulkan::ImageViewResource image_view_resource = vulkan::image_view_resource_create(vk_ctx->device, image_alloc.image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT, 1);

    // Transition image to SHADER_READ_ONLY_OPTIMAL
    VkCommandBuffer cmd;
    VkCommandBufferAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandPool = vk_ctx->command_pool;
    alloc_info.commandBufferCount = 1;
    vkAllocateCommandBuffers(vk_ctx->device, &alloc_info, &cmd);

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin_info);

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image_alloc.image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &cmd;
    vkQueueSubmit(vk_ctx->graphics_queue, 1, &submit_info, VK_NULL_HANDLE);
    vkQueueWaitIdle(vk_ctx->graphics_queue);

    vkFreeCommandBuffers(vk_ctx->device, vk_ctx->command_pool, 1, &cmd);

    vk_ctx->null_texture_handle = render::texture_handle_create(&sampler_info);
    render::AssetItem<vulkan::TextureHandle>* tex_asset = vulkan::asset_manager_texture_item_get(vk_ctx->null_texture_handle);
    AssertAlways(tex_asset);
    tex_asset->item.image_resource = vulkan::ImageResource(image_alloc, image_view_resource);
    tex_asset->item.staging_allocation = {};
    tex_asset->is_loaded = 1;

    vulkan::descriptor_set_update_bindless_texture(tex_asset->item.descriptor_set_idx, image_view_resource.image_view, tex_asset->item.sampler);
}

// ~mgj: Vulkan Interface
static void
render_ctx_create(String8 shader_path, io::IO* io_ctx, async::ThreadPool* thread_pool)
{
    ScratchScope scratch = ScratchScope(0, 0);

    Arena* arena = arena_alloc();
    Debug_SetName(arena, "vulkan context arena");

    vulkan::Context* vk_ctx = PushStruct(arena, vulkan::Context);
    vulkan::ctx_set(vk_ctx);
    vk_ctx->arena = arena;
    vk_ctx->render_thread_id = os_tid();

    const char* validation_layers[] = {"VK_LAYER_KHRONOS_validation", "VK_LAYER_KHRONOS_synchronization2"};
    vk_ctx->validation_layers = buffer_alloc<String8>(vk_ctx->arena, ArrayCount(validation_layers));
    for (U32 i = 0; i < ArrayCount(validation_layers); i++)
    {
        vk_ctx->validation_layers.data[i] = {str8_c_string(validation_layers[i])};
    }

    const char* device_extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME,           VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME, VK_EXT_COLOR_WRITE_ENABLE_EXTENSION_NAME,
                                       VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME,   VK_EXT_MEMORY_BUDGET_EXTENSION_NAME};
    vk_ctx->device_extensions = buffer_alloc<String8>(vk_ctx->arena, ArrayCount(device_extensions));
    for (U32 i = 0; i < ArrayCount(device_extensions); i++)
    {
        vk_ctx->device_extensions.data[i] = {str8_c_string(device_extensions[i])};
    }

    vulkan::create_instance(vk_ctx);
    vulkan::debug_messenger_setup(vk_ctx);
    vulkan::surface_create(vk_ctx, io_ctx);
    vulkan::physical_device_pick(vk_ctx);
    vulkan::logical_device_create(scratch.arena, vk_ctx);

    // ~mgj: Blitting format
    vk_ctx->blit_format = VK_FORMAT_R8G8B8A8_SRGB;
    VkFormatProperties formatProperties;
    vkGetPhysicalDeviceFormatProperties(vk_ctx->physical_device, vk_ctx->blit_format, &formatProperties);
    if (!(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))
    {
        exit_with_error("texture image format does not support linear blitting!");
    }

    vk_ctx->object_id_format = VK_FORMAT_R32G32_UINT;

    vk_ctx->max_texture_count = 5000;
    vulkan::descriptor_pool_create(vk_ctx, vk_ctx->max_texture_count);

    // ~mgj: Create asset manager (includes VMA allocator)
    vk_ctx->asset_manager = vulkan::asset_manager_create(vk_ctx->physical_device, vk_ctx->device, vk_ctx->instance, vk_ctx->graphics_queue, vk_ctx->queue_family_indices.graphicsFamilyIndex,
                                                         thread_pool, GB(1), vk_ctx->descriptor_pool);

    Vec2S32 vk_framebuffer_dim_s32 = io::wait_for_valid_framebuffer_size(io_ctx);
    Vec2U32 vk_framebuffer_dim_u32 = {(U32)vk_framebuffer_dim_s32.x, (U32)vk_framebuffer_dim_s32.y};
    vulkan::SwapChainSupportDetails swapchain_details = vulkan::query_swapchain_support(scratch.arena, vk_ctx->physical_device, vk_ctx->surface);
    VkExtent2D swapchain_extent = vulkan::choose_swap_extent(vk_framebuffer_dim_u32, swapchain_details.capabilities);
    vk_ctx->swapchain_resources = vulkan::swapchain_create(vk_ctx, &swapchain_details, swapchain_extent);

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = vk_ctx->queue_family_indices.graphicsFamilyIndex;
    vk_ctx->command_pool = vulkan::command_pool_create(vk_ctx->device, &poolInfo);

    vulkan::command_buffers_create(vk_ctx);

    vulkan::sync_objects_create(vk_ctx);

    vulkan::profile_buffers_create(vk_ctx);

    // ~mgj: Drawing (TODO: Move out of vulkan context to own module)

    vk_ctx->texture_binding = 0;
    vk_ctx->bindless_descriptor_set_layout = vulkan::descriptor_set_layout_create_bindless_textures(vk_ctx->device, vk_ctx->texture_binding, vk_ctx->max_texture_count,
                                                                                                    VK_SHADER_STAGE_ALL); // TODO: VK_SHADER_STAGE_ALL is probably not the best flag
    vk_ctx->bindless_descriptor_set = vulkan::descriptor_set_allocate_bindless(vk_ctx->device, vk_ctx->descriptor_pool, vk_ctx->bindless_descriptor_set_layout, vk_ctx->max_texture_count);
    camera_descriptor_set_layout_create(vk_ctx);

    null_texture_create(vk_ctx);

    vk_ctx->render_frame_arena = arena_alloc();
    Debug_SetName(vk_ctx->render_frame_arena, "vulkan render frame arena");
    vk_ctx->model_3D_pipeline = vulkan::model_3d_pipeline_create(vk_ctx, shader_path);
    vk_ctx->car_instance_pipeline = vulkan::car_instance_pipeline_create(vk_ctx, shader_path);
    vk_ctx->blend_3d_pipeline = vulkan::blend_3d_pipeline_create(shader_path);
    vk_ctx->road_intersection_pipeline = vulkan::road_intersection_pipeline_create(shader_path);
    vk_ctx->car_height_calculate_pipeline = vulkan::car_instance_compute_pipeline_create(shader_path);
}

static void
render_ctx_destroy()
{
    vulkan::Context* vk_ctx = vulkan::ctx_get();
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    vkDeviceWaitIdle(vk_ctx->device);

    vulkan::profile_buffers_destroy(vk_ctx);

    // Asset manager must be destroyed first — it flushes pending GPU commands
    // that reference descriptor sets, pipelines, and other Vulkan resources

    for (U32 i = 0; i < ArrayCount(vk_ctx->model_3D_instance_buffer); i++)
    {
        render::handle_destroy_deferred(vk_ctx->model_3D_instance_buffer[i]);
    }

    vulkan::swapchain_cleanup(vk_ctx->device, vk_ctx->swapchain_resources);

    render::handle_destroy(vk_ctx->null_texture_handle);
    vulkan::asset_manager_destroy(vk_ctx->asset_manager);
    vkDestroyCommandPool(vk_ctx->device, vk_ctx->command_pool, nullptr);

    vkDestroySurfaceKHR(vk_ctx->instance, vk_ctx->surface, nullptr);
    for (U32 i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        vkDestroyFence(vk_ctx->device, vk_ctx->in_flight_fences.data[i], nullptr);
    }

    vkDestroyDescriptorPool(vk_ctx->device, vk_ctx->descriptor_pool, 0);

    vulkan::pipeline_destroy(&vk_ctx->model_3D_pipeline);
    vulkan::pipeline_destroy(&vk_ctx->car_instance_pipeline);
    vulkan::pipeline_destroy(&vk_ctx->blend_3d_pipeline);
    vulkan::pipeline_destroy(&vk_ctx->road_intersection_pipeline);
    vulkan::pipeline_destroy(&vk_ctx->car_height_calculate_pipeline);
    vulkan::pipeline_destroy(&vk_ctx->bbox_pipeline);

    vkDestroyDescriptorSetLayout(vk_ctx->device, vk_ctx->bindless_descriptor_set_layout, nullptr);
    vkDestroyDescriptorSetLayout(vk_ctx->device, vk_ctx->camera_descriptor_set_layout, nullptr);
    vkDestroyDescriptorSetLayout(vk_ctx->device, vk_ctx->road_segment_descriptor_set_layout, nullptr);
    vkDestroyDescriptorSetLayout(vk_ctx->device, vk_ctx->storage_buffer_descriptor_set_layout, nullptr);
    vkDestroyDescriptorSetLayout(vk_ctx->device, vk_ctx->car_height_calculate_descriptor_set_layout, nullptr);

    vkDestroyDevice(vk_ctx->device, nullptr);
    if (vk_ctx->enable_validation_layers)
    {
        vulkan::destroy_debug_utils_messenger_ext(vk_ctx->instance, vk_ctx->debug_messenger, nullptr);
    }
    vkDestroyInstance(vk_ctx->instance, nullptr);
    vulkan::ctx_release();

    arena_release(vk_ctx->render_frame_arena);

    arena_release(vk_ctx->arena);
}

static void
render_frame(Vec2U32 framebuffer_dim, B32* in_out_framebuffer_resized, Vec2S64 mouse_cursor_pos)
{
    prof_scope_marker;
    ScratchScope scratch = ScratchScope(0, 0);
    vulkan::Context* vk_ctx = vulkan::ctx_get();

    prof_frame_marker;
    if (framebuffer_dim.x == 0 || framebuffer_dim.y == 0)
    {
        vk_ctx->mapped_handle_list = {};
        return;
    }
    vmaSetCurrentFrameIndex(vk_ctx->asset_manager->allocator, vk_ctx->current_frame);

    defer({ vk_ctx->current_frame = (vk_ctx->current_frame + 1) % MAX_FRAMES_IN_FLIGHT; });

    vulkan::asset_manager_execute_cmds();
    vulkan::asset_manager_cmd_done_check();

    vulkan::SwapchainResources* swapchain_resources = vk_ctx->swapchain_resources;
    if (!swapchain_resources)
    {
        vulkan::SwapChainSupportDetails swapchain_details = vulkan::query_swapchain_support(scratch.arena, vk_ctx->physical_device, vk_ctx->surface);
        VkExtent2D swapchain_extent = vulkan::choose_swap_extent(framebuffer_dim, swapchain_details.capabilities);
        if (swapchain_extent.width != 0 && swapchain_extent.height != 0)
            vk_ctx->swapchain_resources = vulkan::swapchain_create(vk_ctx, &swapchain_details, swapchain_extent);
        return;
    }
    VkFence* in_flight_fence = &vk_ctx->in_flight_fences.data[vk_ctx->current_frame];
    U32 image_idx = 0;
    VkSemaphore image_available_semaphore = swapchain_resources->image_available_semaphores.data[vk_ctx->current_frame];
    {
        {
            prof_scope_marker_named("Wait for frame");
            VkResult fence_result = vkWaitForFences(vk_ctx->device, 1, in_flight_fence, VK_TRUE, UINT64_MAX);
            VK_CHECK_RESULT(fence_result);
        }

        // delete everyting in the deletion queue as all the command using its ressource have
        // finished execution at this point
        vulkan::deletion_queue_empty_next();
        vulkan::mapped_buffers_update();

        VkResult result = vkAcquireNextImageKHR(vk_ctx->device, vk_ctx->swapchain_resources->swapchain, UINT64_MAX, image_available_semaphore, VK_NULL_HANDLE, &image_idx);

        if (result == VK_ERROR_OUT_OF_DATE_KHR || *in_out_framebuffer_resized)
        {
            *in_out_framebuffer_resized = false;
            vulkan::swapchain_recreate(framebuffer_dim);
            return;
        }
        else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
        {
            exit_with_error("failed to acquire swap chain image!");
        }
    }

    VkFence image_in_flight_fence = swapchain_resources->image_in_flight_fences.data[image_idx];
    if (image_in_flight_fence != VK_NULL_HANDLE)
    {
        VK_CHECK_RESULT(vkWaitForFences(vk_ctx->device, 1, &image_in_flight_fence, VK_TRUE, UINT64_MAX));
    }
    swapchain_resources->image_in_flight_fences.data[image_idx] = *in_flight_fence;

    VkSemaphore render_finished_semaphore = swapchain_resources->render_finished_semaphores.data[image_idx];

    VkCommandBuffer cmd_buffer = vk_ctx->command_buffers.data[vk_ctx->current_frame];
    VK_CHECK_RESULT(vkResetFences(vk_ctx->device, 1, in_flight_fence));
    VK_CHECK_RESULT(vkResetCommandBuffer(cmd_buffer, 0));

    vulkan::command_buffer_record(image_idx, vk_ctx->current_frame, mouse_cursor_pos);

    VkSemaphoreSubmitInfo waitSemaphoreInfo{};
    waitSemaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    waitSemaphoreInfo.semaphore = image_available_semaphore;
    waitSemaphoreInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

    VkSemaphoreSubmitInfo signalSemaphoreInfo{};
    signalSemaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signalSemaphoreInfo.semaphore = render_finished_semaphore;
    signalSemaphoreInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

    VkCommandBufferSubmitInfo commandBufferInfo{};
    commandBufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    commandBufferInfo.commandBuffer = cmd_buffer;

    VkSubmitInfo2 submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submitInfo.waitSemaphoreInfoCount = 1;
    submitInfo.pWaitSemaphoreInfos = &waitSemaphoreInfo;
    submitInfo.signalSemaphoreInfoCount = 1;
    submitInfo.pSignalSemaphoreInfos = &signalSemaphoreInfo;
    submitInfo.commandBufferInfoCount = 1;
    submitInfo.pCommandBufferInfos = &commandBufferInfo;

    VK_CHECK_RESULT(vkQueueSubmit2(vk_ctx->graphics_queue, 1, &submitInfo, *in_flight_fence));

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &render_finished_semaphore;

    VkSwapchainKHR swapChains[] = {vk_ctx->swapchain_resources->swapchain};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &image_idx;
    presentInfo.pResults = nullptr; // Optional

    VkResult result = vkQueuePresentKHR(vk_ctx->present_queue, &presentInfo);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || *in_out_framebuffer_resized)
    {
        *in_out_framebuffer_resized = 0;
        vulkan::swapchain_recreate(framebuffer_dim);
    }
    else if (result != VK_SUCCESS)
    {
        exit_with_error("failed to present swap chain image!");
    }
}

static void
gpu_work_done_wait()
{
    vulkan::Context* vk_ctx = vulkan::ctx_get();
    vkDeviceWaitIdle(vk_ctx->device);
}

static void
new_frame()
{
    vulkan::Context* vk_ctx = vulkan::ctx_get();
    vk_ctx->mapped_handle_list = {};
    arena_clear(vk_ctx->render_frame_arena);
    vk_ctx->render_frame = PushStruct(vk_ctx->render_frame_arena, vulkan::RenderFrame);
    ImGui_ImplVulkan_NewFrame();
}

static U64
latest_hovered_object_id_get()
{
    vulkan::Context* vk_ctx = vulkan::ctx_get();
    return vk_ctx->hovered_object_id;
}

// ~mgj: Texture interface functions
g_internal Handle
texture_zero_handle_get()
{
    vulkan::Context* vk_ctx = vulkan::ctx_get();
    return vk_ctx->null_texture_handle;
}

g_internal Handle
texture_handle_create(SamplerInfo* sampler_info)
{
    vulkan::Context* vk_ctx = vulkan::ctx_get();
    vulkan::AssetManager* asset_manager = vk_ctx->asset_manager;
    // ~mgj: Create sampler
    VkSamplerCreateInfo sampler_create_info = {};
    vulkan::sampler_create_info_from_sampler_info(sampler_info, &sampler_create_info);
    sampler_create_info.anisotropyEnable = VK_TRUE;
    sampler_create_info.maxAnisotropy = (F32)vk_ctx->msaa_samples;
    VkSampler vk_sampler = vulkan::sampler_create(vk_ctx->device, &sampler_create_info);

    // ~mgj: Assign values to texture
    render::Handle asset_handle = render::Handle::texture_handle_create();
    os_mutex_scope_w(asset_manager->texture_mutex)
    {
        AssetItem<vulkan::TextureHandle>* asset_item = (AssetItem<vulkan::TextureHandle>*)asset_handle.ptr;
        vulkan::TextureHandle* texture = &asset_item->item;
        texture->descriptor_set_idx = vulkan::descriptor_index_allocate(&asset_manager->descriptor_index_allocator);
        texture->sampler = vk_sampler;
    }

    return asset_handle;
}

static render::Handle
texture_load_async(render::SamplerInfo* sampler_info, String8 texture_path)
{
    ScratchScope scratch = ScratchScope(0, 0);
    render::ThreadWorkerCmdCtx* thread_input = render::thread_ctx_create();

    // ~mgj: make input ready for texture loading on thread
    render::TextureLoadingInfo* texture_load_info = PushStruct(thread_input->arena, render::TextureLoadingInfo);
    texture_load_info->tex_path = push_str8_copy(thread_input->arena, texture_path);
    thread_input->user_data = texture_load_info;
    render::Handle handle = render::texture_handle_create(sampler_info);
    render::handle_list_push(thread_input, handle);

    thread_input->loading_func = vulkan::texture_loading_from_path_thread;

    async::WorkerItem item = async::WorkerItem(thread_input, vulkan::thread_main);
    async::thread_pool_push(thread_input->thread_pool, &item);

    return handle;
}

g_internal Handle
texture_load_sync(render::ThreadWorkerCmdCtx* thread_ctx, render::SamplerInfo* sampler_info, Buffer<U8> tex_buf)
{
    ScratchScope scratch = ScratchScope(0, 0);

    Assert(tex_buf.size > 0 && "Texture file not found");
    render::Handle tex_handle = render::texture_handle_create(sampler_info);
    B32 err = vulkan::texture_gpu_upload_cmd_recording((VkCommandBuffer)thread_ctx->cmd_buffer, tex_handle, tex_buf);
    if (err)
    {
        ERROR_LOG("Error when uploading texture - Error id: %d\n", err);
    }
    render::handle_list_push(thread_ctx, tex_handle);

    return tex_handle;
}

g_internal Handle
texture_load_sync(render::SamplerInfo* sampler_info, TextureUploadData* tex_data, void* cmd)
{
    prof_scope_marker;
    ScratchScope scratch = ScratchScope(0, 0);

    // ~mgj: make input ready for texture loading on thread
    render::Handle handle = render::texture_handle_create(sampler_info);

    {
        Assert(tex_data);
        vulkan::ImageAllocationResource image_allocation_resource = vulkan::texture_upload_with_blitting((VkCommandBuffer)cmd, tex_data);

        render::AssetItem<vulkan::TextureHandle>* tex_asset = vulkan::asset_manager_item_get<vulkan::TextureHandle>(handle);
        if (tex_asset)
        {
            tex_asset->item.image_resource = image_allocation_resource.image_resource;
            tex_asset->item.staging_allocation = image_allocation_resource.staging_buffer_alloc;
        }
        else
        {
            buffer_destroy(&image_allocation_resource.staging_buffer_alloc);
            image_resource_destroy(image_allocation_resource.image_resource);
        }
    }
    return handle;
}

g_internal void
handle_destroy(render::Handle handle)
{
    handle_destroy_deferred(handle);
}

g_internal void
handle_destroy_deferred(render::Handle handle)
{
    if (render::is_handle_zero(handle) == false)
    {
        vulkan::deletion_queue_push(handle);
    }
}

template <typename T>
g_internal void
mapped_buffer_add(MappedHandle<T> mut_handle, T* data)
{
    vulkan::Context* vk_ctx = vulkan::ctx_get();
    String8 buffer = str8((U8*)data, sizeof(T));
    String8 source = push_str8_copy(vk_ctx->render_frame_arena, buffer);
    LinkedListNode<vulkan::MappedHandleTransfer>* mut_handle_node = PushStruct(vk_ctx->render_frame_arena, LinkedListNode<vulkan::MappedHandleTransfer>);
    render::MappedHandle<void> handle_void = render::mapped_handle_erased(mut_handle);
    mut_handle_node->v.mapped_handle = handle_void;
    mut_handle_node->v.source = source;
    SLLQueuePush(vk_ctx->mapped_handle_list.first, vk_ctx->mapped_handle_list.last, mut_handle_node);
}

template <typename T>
g_internal MappedHandle<T>
mapped_buffer_create(Arena* arena, render::ThreadWorkerCmdCtx* thread_ctx, BufferType buffer_type, String8 debug_name)
{
    ScratchScope scratch = ScratchScope(0, 0);
    Buffer<MappedHandleFrame<T>> handle_buffer = buffer_alloc<MappedHandleFrame<T>>(arena, render::MAX_FRAMES_IN_FLIGHT);

    for (U32 frame_idx = 0; frame_idx < handle_buffer.size; ++frame_idx)
    {
        MappedHandleFrame<T>* content = &handle_buffer.data[frame_idx];
        VmaAllocationCreateInfo vma_info = {0};
        vma_info.usage = VMA_MEMORY_USAGE_AUTO;
        vma_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        vma_info.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;

        BufferInfo buffer_info = BufferInfo::empty_buffer_info<T>(arena, buffer_type);
        content->handle = vulkan::asset_manager_buffer_allocation_create(thread_ctx, &buffer_info, vma_info);
        render::AssetItem<vulkan::BufferHandle>* asset_item_buffer = vulkan::asset_manager_buffer_item_get(content->handle);
        vulkan::BufferHandle* buffer_handle = &asset_item_buffer->item;

#if BUILD_DEBUG
        String8 frame_debug_name = push_str8f(scratch.arena, "%.*s[%u]", str8_varg(debug_name), frame_idx);
        vulkan::asset_manager_debug_name_set(buffer_handle->buffer_alloc.allocation, frame_debug_name);
#endif

        content->data = (T*)vulkan::asset_manager_allocation_cpu_pointer_get(buffer_handle->buffer_alloc.allocation);
        AssertAlways(content->data);
    }

    MappedHandle<T> mut_handle = {};
    mut_handle.buffer = handle_buffer;
    return mut_handle;
}

template <typename T>
g_internal void
mapped_buffer_destroy(MappedHandle<T> mapped_handle)
{
    for (auto h : mapped_handle.buffer)
    {
        render::handle_destroy(h.handle);
    }
}
g_internal Handle
_buffer_load_sync(render::ThreadWorkerCmdCtx* thread_ctx, render::BufferInfo* buffer_info, String8 debug_name)
{
    prof_scope_marker;
    if (buffer_info->buffer.size == 0)
    {
        DEBUG_LOG("Zero handle created for buffer\n");
        InvalidPath;
        return render::handle_zero();
    }

    VmaAllocationCreateInfo vma_info = {0};
    vma_info.usage = VMA_MEMORY_USAGE_AUTO;
    vma_info.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    Handle handle = vulkan::asset_manager_buffer_allocation_create(thread_ctx, buffer_info, vma_info);
    render::AssetItem<vulkan::BufferHandle>* asset_item_buffer = vulkan::asset_manager_buffer_item_get(handle);
    vulkan::BufferHandle* buffer_handle = &asset_item_buffer->item;

    // ~mgj: Create staging buffer allocation
    buffer_handle->staging_buffer = vulkan::asset_manager_buffer_from_staging((VkCommandBuffer)thread_ctx->cmd_buffer, buffer_info, buffer_handle->buffer_alloc.buffer);

#if BUILD_DEBUG
    ScratchScope scratch = ScratchScope(0, 0);
    vulkan::asset_manager_debug_name_set(buffer_handle->buffer_alloc.allocation, debug_name);
    String8 staging_debug_name = str8_concat(scratch.arena, debug_name, S("(staging buffer)"));
    vulkan::asset_manager_debug_name_set(buffer_handle->staging_buffer.allocation, staging_debug_name);
#endif

    return handle;
}

static render::Handle
buffer_load_async(render::BufferInfo* buffer_info)
{
    prof_scope_marker;
    if (buffer_info->buffer.size == 0)
    {
        DEBUG_LOG("Zero handle created for buffer\n");
        InvalidPath;
        return render::handle_zero();
    }

    vulkan::Context* vk_ctx = vulkan::ctx_get();
    vulkan::AssetManager* asset_manager = vk_ctx->asset_manager;
    render::BufferType buffer_type = (render::BufferType)buffer_info->buffer_type;
    render::Handle asset_handle = render::Handle::buffer_handle_create(buffer_type);

    // ~mgj: Create buffer allocation
    VmaAllocationCreateInfo vma_info = {0};
    vma_info.usage = VMA_MEMORY_USAGE_AUTO;
    vma_info.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    VkBufferUsageFlags usage_flags = {};
    if (buffer_type & render::BufferType_Vertex)
        usage_flags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (buffer_type & render::BufferType_Index)
        usage_flags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (buffer_type & render::BufferType_Uniform)
        usage_flags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if (buffer_type & render::BufferType_StorageBuffer)
        usage_flags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    Assert(usage_flags != 0);
    vulkan::BufferAllocation buffer = vulkan::_buffer_allocation_create(buffer_info->buffer.size, usage_flags | VK_BUFFER_USAGE_TRANSFER_DST_BIT, &vma_info, nullptr);

    // ~mgj: Prepare buffer asset item
    os_mutex_scope_w(asset_manager->buffer_mutex)
    {
        AssetItem<vulkan::BufferHandle>* asset_item = (AssetItem<vulkan::BufferHandle>*)asset_handle.ptr;
        vulkan::BufferHandle* asset_buffer = (vulkan::BufferHandle*)&asset_item->item;
        asset_buffer->buffer_alloc = buffer;
        asset_buffer->item_byte_size = buffer_info->type_size;
        asset_buffer->elem_count = buffer_info->elem_count;
    }

    // ~mgj: Preparing buffer loading for another thread
    render::ThreadWorkerCmdCtx* thread_input = render::thread_ctx_create();
    render::handle_list_push(thread_input, asset_handle);

    // Copy buffer_info to thread_input arena to ensure it persists for async execution
    render::BufferInfo* buffer_info_copy = PushStruct(thread_input->arena, render::BufferInfo);
    *buffer_info_copy = *buffer_info;
    buffer_info_copy->buffer = buffer_alloc<U8>(thread_input->arena, buffer_info->buffer.size);
    MemoryCopy(buffer_info_copy->buffer.data, buffer_info->buffer.data, buffer_info->buffer.size);

    thread_input->user_data = buffer_info_copy;
    thread_input->loading_func = vulkan::buffer_loading_thread;

    async::WorkerItem item = async::WorkerItem(thread_input, vulkan::thread_main);
    async::thread_pool_push(thread_input->thread_pool, &item);

    return asset_handle;
}

g_internal void
agent_instance_compute_bucket_add(render::BufferInfo* instance_buffer_info, render::Handle tile_vertex_buffer_handle, render::Handle tile_index_buffer_handle, F32 car_center_to_road_offset,
                                  U32 instance_buffer_offset)
{
    if (instance_buffer_info->buffer.size == 0 || instance_buffer_info->elem_count == 0)
    {
        return;
    }

    vulkan::Context* vk_ctx = vulkan::ctx_get();
    vulkan::RenderFrame* render_frame = vk_ctx->render_frame;
    vulkan::CarInstanceCompute* instance_draw = &render_frame->car_instance_compute_list;

    render::AssetItem<vulkan::BufferHandle>* asset_tile_vertex = 0;
    render::AssetItem<vulkan::BufferHandle>* asset_tile_index = 0;
    if (render::is_resource_loaded(tile_vertex_buffer_handle, &asset_tile_vertex) && render::is_resource_loaded(tile_index_buffer_handle, &asset_tile_index))
    {
        vulkan::CarInstanceComputeNode* node = PushStruct(vk_ctx->render_frame_arena, vulkan::CarInstanceComputeNode);

        // compute ressources
        vulkan::CarHeightCalculatePushConstants compute_push_constants = {.car_count = instance_buffer_info->elem_count, .agent_center_offset = car_center_to_road_offset};
        node->tile_index_handle = &asset_tile_index->item;
        node->tile_vertex_handle = &asset_tile_vertex->item;
        node->compute_push_constants = compute_push_constants;
        node->instance_buffer_info = *instance_buffer_info;
        node->instance_buffer_offset = instance_buffer_offset;

        // push work
        SLLQueuePush(instance_draw->list.first, instance_draw->list.last, node);
    }
}

g_internal bool
agent_instance_render_bucket_add(render::MappedHandle<void> camera_handle, Buffer<render::MeshHandlePair> meshes, Buffer<render::Handle> texture_handles, render::BufferInfo* instance_buffer_info,
                                 U32 instance_buffer_offset)
{
    if (instance_buffer_info->buffer.size == 0 || instance_buffer_info->elem_count == 0 || meshes.size == 0 || texture_handles.size == 0)
    {
        return false;
    }

    vulkan::Context* vk_ctx = vulkan::ctx_get();
    vulkan::RenderFrame* render_frame = vk_ctx->render_frame;
    vulkan::CarInstanceRender* instance_draw = &render_frame->car_instance_render_list;

    B32 resources_loaded = true;
    for (U32 mesh_idx = 0; mesh_idx < meshes.size; ++mesh_idx)
    {
        render::MeshHandlePair* mesh = meshes[mesh_idx];
        if (mesh->texture_handle_idx >= texture_handles.size)
        {
            return false;
        }
        render::Handle texture_handle = texture_handles.data[mesh->texture_handle_idx];
        resources_loaded = resources_loaded && render::is_resource_loaded(mesh->vertex_handle) && render::is_resource_loaded(mesh->index_handle) && render::is_resource_loaded(texture_handle);
    }

    if (resources_loaded)
    {
        // draw ressources
        vulkan::CarInstanceRenderNode* node = PushStruct(vk_ctx->render_frame_arena, vulkan::CarInstanceRenderNode);
        node->meshes = meshes;
        node->texture_handles = texture_handles;
        node->instance_buffer_info = *instance_buffer_info;
        node->instance_buffer_offset = instance_buffer_offset;
        node->camera_handle = camera_handle;
        instance_draw->total_instance_buffer_byte_count = Max(instance_draw->total_instance_buffer_byte_count, instance_buffer_offset + instance_buffer_info->buffer.size);

        // push work
        SLLQueuePush(instance_draw->list.first, instance_draw->list.last, node);
        return true;
    }

    return false;
}

g_internal bool
road_intersection_compute_add(Handle vertex_buffer_handle, Handle index_buffer_handle, Handle road_segment_buffer_handle, Handle road_segment_node_buffer_handle, U32 overlay_option)
{
    bool compute_scheduled = false;

    render::AssetItem<vulkan::BufferHandle>* vertex_buffer = 0;
    render::AssetItem<vulkan::BufferHandle>* index_buffer = 0;
    render::AssetItem<vulkan::BufferHandle>* road_segment_buffer = 0;
    render::AssetItem<vulkan::BufferHandle>* road_segment_node_buffer = 0;
    if (is_resource_loaded(vertex_buffer_handle, &vertex_buffer) && is_resource_loaded(index_buffer_handle, &index_buffer) && is_resource_loaded(road_segment_buffer_handle, &road_segment_buffer) &&
        is_resource_loaded(road_segment_node_buffer_handle, &road_segment_node_buffer))
    {
        compute_scheduled = true;
        vulkan::road_intersection_bucket_add(&vertex_buffer->item, &index_buffer->item, &road_segment_buffer->item, &road_segment_node_buffer->item, overlay_option);
    }
    return compute_scheduled;
}

static void
tile_pipeline_add(render::Model3DPipelineData* pipeline_input)
{
    vulkan::Context* vk_ctx = vulkan::ctx_get();
    vulkan::RenderFrame* render_frame = vk_ctx->render_frame;

    render::AssetItem<vulkan::BufferHandle>* asset_vertex_buffer = 0;
    render::AssetItem<vulkan::BufferHandle>* asset_index_buffer = 0;
    render::AssetItem<vulkan::TextureHandle>* asset_base_texture = 0;
    render::AssetItem<vulkan::BufferHandle>* asset_colormap = 0;
    render::AssetItem<vulkan::TextureHandle>* overlay_tex = 0;

    B32 overlay_tex_loaded = render::is_resource_loaded(pipeline_input->overlay_texture_handle, &overlay_tex);
    B32 vertex_loaded = render::is_resource_loaded(pipeline_input->vertex_buffer_handle, &asset_vertex_buffer);
    B32 index_loaded = render::is_resource_loaded(pipeline_input->index_buffer_handle, &asset_index_buffer);
    B32 base_texture_loaded = render::is_resource_loaded(pipeline_input->texture_handle, &asset_base_texture);
    B32 colormap_loaded = render::is_resource_loaded(pipeline_input->colormap_handle, &asset_colormap);

    B32 overlay_enabled = false;
    B32 colormap_enabled = has_flag(pipeline_input->pipeline_bits, render::TilePipelineBits::ColormapEnabled);
    B32 colormap_ready = colormap_loaded || colormap_enabled == false;
    if (vertex_loaded && index_loaded && base_texture_loaded && colormap_ready)
    {
        B32 overlay_uv_enabled = has_flag(pipeline_input->pipeline_bits, render::TilePipelineBits::OverlayEnabled);
        if (overlay_uv_enabled && pipeline_input->overlay_texture_coordinate_id == 0 && render::is_handle_zero(pipeline_input->overlay_texture_handle) == false)
        {
            overlay_enabled = overlay_tex_loaded;
        }

        vulkan::Model3dPushConstants push_constants = {};
        push_constants.tex_idx = asset_base_texture->item.descriptor_set_idx;
        push_constants.overlay_tex_idx = overlay_tex_loaded ? overlay_tex->item.descriptor_set_idx : 0;
        push_constants.overlay_enabled = overlay_enabled;
        if (colormap_enabled)
        {
            push_constants.colormap_address = asset_colormap->item.buffer_alloc.device_address;
            push_constants.colormap_len = asset_colormap->item.buffer_alloc.size / (3 * sizeof(F32));
        }
        push_constants.overlay_translation_x = pipeline_input->overlay_translation.x;
        push_constants.overlay_translation_y = pipeline_input->overlay_translation.y;
        push_constants.overlay_scale_x = pipeline_input->overlay_scale.x;
        push_constants.overlay_scale_y = pipeline_input->overlay_scale.y;
        push_constants.height_offset = pipeline_input->height_offset;

        vulkan::Model3DNode* node = PushStruct(vk_ctx->render_frame_arena, vulkan::Model3DNode);
        node->vertex_alloc = asset_vertex_buffer->item.buffer_alloc;
        node->index_alloc = asset_index_buffer->item.buffer_alloc;
        node->push_constants = push_constants;
        node->index_count = pipeline_input->index_count;
        node->index_buffer_offset = pipeline_input->index_offset;
        node->camera_handle = pipeline_input->camera_handle;
        node->overwrite_depth = has_flag(pipeline_input->pipeline_bits, TilePipelineBits::OverwriteDepth);

        SLLQueuePush(render_frame->model_3D_list.first, render_frame->model_3D_list.last, node);
    }
    else
    {
        DEBUG_LOG("overlay texture: %d, vertex buffer: %d, index buffer: %d, base texture: %d, colormap texture: %d", overlay_tex_loaded, vertex_loaded, index_loaded, base_texture_loaded,
                  colormap_loaded);
    }
}

lib_internal void
blend_3d_draw(render::Blend3DPipelineData pipeline_input)
{
    render::AssetItem<vulkan::BufferHandle>* asset_vertex_buffer = 0;
    render::AssetItem<vulkan::BufferHandle>* asset_index_buffer = 0;
    render::AssetItem<vulkan::TextureHandle>* asset_texture = 0;
    render::AssetItem<vulkan::TextureHandle>* asset_colormap = 0;
    if (render::is_resource_loaded(pipeline_input.index_buffer_handle, &asset_index_buffer) && render::is_resource_loaded(pipeline_input.vertex_buffer_handle, &asset_vertex_buffer) &&
        render::is_resource_loaded(pipeline_input.texture_handle, &asset_texture) && render::is_resource_loaded(pipeline_input.colormap_handle, &asset_colormap))
    {
        vulkan::blend_3d_bucket_add(&asset_vertex_buffer->item.buffer_alloc, &asset_index_buffer->item.buffer_alloc, pipeline_input.texture_handle, pipeline_input.colormap_handle,
                                    pipeline_input.camera_handle);
    }
}

g_internal render::AssetItem<vulkan::BufferHandle>*
_render_asset_item_get(render::Handle handle, vulkan::BufferHandle*)
{
    return handle.type == render::HandleType::Buffer ? vulkan::asset_manager_buffer_item_get(handle) : 0;
}

g_internal render::AssetItem<vulkan::TextureHandle>*
_render_asset_item_get(render::Handle handle, vulkan::TextureHandle*)
{
    return handle.type == render::HandleType::Texture ? vulkan::asset_manager_texture_item_get(handle) : 0;
}

template <typename T>
g_internal bool
is_resource_loaded(render::Handle handle, render::AssetItem<T>** out_asset)
{
    if (out_asset)
    {
        *out_asset = 0;
    }

    if (render::is_handle_zero(handle))
    {
        return false;
    }

    T* type_marker = 0;
    render::AssetItem<T>* asset = _render_asset_item_get(handle, type_marker);
    if (out_asset)
    {
        *out_asset = asset;
    }

    return asset ? asset->is_loaded : false;
}

g_internal bool
is_resource_loaded(render::Handle handle)
{
    switch (handle.type)
    {
        case render::HandleType::Buffer:
        {
            render::AssetItem<vulkan::BufferHandle>* asset = 0;
            return is_resource_loaded(handle, &asset);
        }
        case render::HandleType::Texture:
        {
            render::AssetItem<vulkan::TextureHandle>* asset = 0;
            return is_resource_loaded(handle, &asset);
        }
        default: break;
    }

    return false;
}

Handle
Handle::texture_handle_create()
{
    vulkan::AssetManager* asset_manager = vulkan::asset_manager_get();
    os_rw_mutex_take_w(asset_manager->texture_mutex);
    Handle handle = asset_manager_item_create(&asset_manager->texture_list, &asset_manager->texture_free_list, render::HandleType::Texture);
    os_rw_mutex_drop_w(asset_manager->texture_mutex);
    return handle;
}

Handle
Handle::buffer_handle_create(BufferType buffer_type)
{
    vulkan::AssetManager* asset_manager = vulkan::asset_manager_get();
    os_rw_mutex_take_w(asset_manager->buffer_mutex);
    Handle handle = asset_manager_item_create(&asset_manager->buffer_list, &asset_manager->buffer_free_list, render::HandleType::Buffer);
    render::AssetItem<vulkan::BufferHandle>* asset_item = (render::AssetItem<vulkan::BufferHandle>*)handle.ptr;
    asset_item->item.type = buffer_type;

    os_rw_mutex_drop_w(asset_manager->buffer_mutex);
    return handle;
}

g_internal void
thread_cmd_buffer_record(ThreadWorkerCmdCtx* thread_ctx)
{
    vulkan::AssetManager* asset_manager = vulkan::asset_manager_get();

    U32 thread_local_id = async::t_cur_thread_id;
    // ~mgj: Record the command buffer
    vulkan::AssetManagerCommandPool thread_cmd_pool = vulkan::asset_manager_cmd_pool_get(asset_manager, thread_local_id);
    thread_ctx->cmd_buffer = begin_command(asset_manager->device, &thread_cmd_pool);
}

g_internal void
thread_cmd_buffer_end(ThreadWorkerCmdCtx* cmd_ctx)
{
    U32 thread_local_id = async::t_cur_thread_id;
    vulkan::AssetManager* asset_manager = vulkan::asset_manager_get();
    vulkan::AssetManagerCommandPool thread_cmd_pool = vulkan::asset_manager_cmd_pool_get(asset_manager, thread_local_id);
    end_command(&thread_cmd_pool, (VkCommandBuffer)cmd_ctx->cmd_buffer);
    vulkan::asset_cmd_queue_item_enqueue(thread_local_id, cmd_ctx);
}

g_internal void
handle_done_loading(render::HandleList handles)
{
    for (render::HandleNode* node = handles.first; node; node = node->next)
    {
        render::Handle handle = node->handle;
        switch (handle.type)
        {
            case render::HandleType::Buffer:
            {
                render::AssetItem<vulkan::BufferHandle>* asset = vulkan::asset_manager_buffer_item_get(handle);
                if (asset)
                {
                    buffer_destroy(&asset->item.staging_buffer);
                    asset->item.staging_buffer.buffer = 0;
                    asset->is_loaded = 1;
                    node->work_on_gpu_done = 1;
                }
            }
            break;
            case render::HandleType::Texture:
            {
                render::AssetItem<vulkan::TextureHandle>* asset = vulkan::asset_manager_texture_item_get(handle);
                if (asset)
                {
                    vulkan::TextureHandle* texture = &asset->item;
                    vulkan::descriptor_set_update_bindless_texture(texture->descriptor_set_idx, texture->image_resource.image_view_resource.image_view, texture->sampler);
                    buffer_destroy(&asset->item.staging_allocation);
                    asset->is_loaded = 1;
                    node->work_on_gpu_done = 1;
                }
            }
            break;
            default: InvalidPath; break;
        }
    }
}

} // namespace render

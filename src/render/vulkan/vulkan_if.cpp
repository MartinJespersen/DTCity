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

    const char* device_extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME, VK_EXT_COLOR_WRITE_ENABLE_EXTENSION_NAME, VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
                                       VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME};
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

    vulkan::camera_uniform_buffer_create(vk_ctx);
    vulkan::camera_descriptor_set_layout_create(vk_ctx);
    vulkan::camera_descriptor_set_create(vk_ctx);
    vulkan::profile_buffers_create(vk_ctx);

    // ~mgj: Drawing (TODO: Move out of vulkan context to own module)

    vk_ctx->texture_binding = 0;
    vk_ctx->bindless_descriptor_set_layout = vulkan::descriptor_set_layout_create_bindless_textures(vk_ctx->device, vk_ctx->texture_binding, vk_ctx->max_texture_count,
                                                                                                    VK_SHADER_STAGE_ALL); // TODO: VK_SHADER_STAGE_ALL is probably not the best flag
    vk_ctx->bindless_descriptor_set = vulkan::descriptor_set_allocate_bindless(vk_ctx->device, vk_ctx->descriptor_pool, vk_ctx->bindless_descriptor_set_layout, vk_ctx->max_texture_count);

    null_texture_create(vk_ctx);

    vk_ctx->render_frame_arena = arena_alloc();
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

    vulkan::camera_cleanup(vk_ctx);
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
    for (U32 i = 0; i < vulkan::MAX_FRAMES_IN_FLIGHT; i++)
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
render_frame(Vec2U32 framebuffer_dim, B32* in_out_framebuffer_resized, ui::Camera* camera, Vec2S64 mouse_cursor_pos)
{
    ScratchScope scratch = ScratchScope(0, 0);

    if (framebuffer_dim.x == 0 || framebuffer_dim.y == 0)
    {
        return;
    }
    vulkan::Context* vk_ctx = vulkan::ctx_get();

    defer({ vk_ctx->current_frame = (vk_ctx->current_frame + 1) % vulkan::MAX_FRAMES_IN_FLIGHT; });

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

    vulkan::command_buffer_record(image_idx, vk_ctx->current_frame, camera, mouse_cursor_pos);

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
    prof_frame_marker; // end of frame is assumed to be here
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
    OS_MutexScopeW(asset_manager->texture_mutex)
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
    vulkan::Context* vk_ctx = vulkan::ctx_get();
    vulkan::AssetManager* asset_manager = vk_ctx->asset_manager;
    render::ThreadWorkerCmdCtx* thread_input = render::thread_input_create();

    // ~mgj: make input ready for texture loading on thread
    render::TextureLoadingInfo* texture_load_info = PushStruct(thread_input->arena, render::TextureLoadingInfo);
    texture_load_info->tex_path = push_str8_copy(thread_input->arena, texture_path);
    thread_input->user_data = texture_load_info;
    render::Handle handle = render::texture_handle_create(sampler_info);
    render::handle_list_push(thread_input->arena, &thread_input->handles, handle);

    thread_input->loading_func = vulkan::texture_loading_from_path_thread;
    thread_input->done_loading_func = render::handle_done_loading;

    async::thread_pool_push(thread_input->arena, thread_input, vulkan::thread_main, asset_manager->threads);

    return handle;
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

g_internal render::Handle
texture_load_async(render::SamplerInfo* sampler_info, TextureUploadData* tex_upload_info)
{
    prof_scope_marker;
    ScratchScope scratch = ScratchScope(0, 0);
    vulkan::Context* vk_ctx = vulkan::ctx_get();
    vulkan::AssetManager* asset_manager = vk_ctx->asset_manager;
    render::ThreadWorkerCmdCtx* thread_input = render::thread_input_create();

    // ~mgj: make input ready for texture loading on thread
    TextureUploadData* tex_upload_data = PushStruct(thread_input->arena, TextureUploadData);
    *tex_upload_data = *tex_upload_info;
    tex_upload_data->data = PushArray(thread_input->arena, U8, tex_upload_info->data_byte_size);
    MemoryCopy(tex_upload_data->data, tex_upload_info->data, tex_upload_info->data_byte_size);

    thread_input->user_data = tex_upload_data;
    render::Handle handle = render::texture_handle_create(sampler_info);
    render::handle_list_push(thread_input->arena, &thread_input->handles, handle);

    thread_input->loading_func = vulkan::texture_loading_thread;
    thread_input->done_loading_func = render::handle_done_loading;

    async::thread_pool_push(thread_input->arena, thread_input, vulkan::thread_main, asset_manager->threads);

    return handle;
}

static render::Handle
colormap_load_async(render::SamplerInfo* sampler_info, const U8* colormap_data, U64 colormap_size)
{
    vulkan::Context* vk_ctx = vulkan::ctx_get();
    vulkan::AssetManager* asset_manager = vk_ctx->asset_manager;
    render::ThreadWorkerCmdCtx* thread_input = render::thread_input_create();

    // ~mgj: make input ready for colormap loading on thread
    render::ColorMapLoadingInfo* colormap_load_info = PushStruct(thread_input->arena, render::ColorMapLoadingInfo);
    colormap_load_info->colormap_data = colormap_data;
    colormap_load_info->colormap_size = colormap_size;

    render::Handle handle = render::texture_handle_create(sampler_info);
    render::handle_list_push(thread_input->arena, &thread_input->handles, handle);
    thread_input->user_data = colormap_load_info;
    thread_input->loading_func = vulkan::colormap_loading_thread;
    thread_input->done_loading_func = render::handle_done_loading;

    async::thread_pool_push(thread_input->arena, thread_input, vulkan::thread_main, asset_manager->threads);

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

g_internal Handle
buffer_load_sync(void* cmd, render::BufferInfo* buffer_info)
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
    U32 type = buffer_info->buffer_type;
    if (type & render::BufferType_Vertex)
        usage_flags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (type & render::BufferType_Index)
        usage_flags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (type & render::BufferType_Uniform)
        usage_flags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if (type & render::BufferType_StorageBuffer)
        usage_flags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    Assert(usage_flags != 0);
    vulkan::BufferAllocation buffer_alloc = vulkan::buffer_allocation_create(buffer_info->buffer.size, usage_flags | VK_BUFFER_USAGE_TRANSFER_DST_BIT, vma_info, "buffer_load_sync");

    // ~mgj: Create staging buffer allocation
    vulkan::BufferAllocation staging_buffer_alloc = vulkan::staging_buffer_create(buffer_info->buffer.size);

    // ~mgj: Prepare buffer asset item
    AssetItem<vulkan::BufferHandle>* asset_item = (AssetItem<vulkan::BufferHandle>*)asset_handle.ptr;
    if (asset_item)
    {
        vulkan::BufferHandle* asset_buffer = (vulkan::BufferHandle*)&asset_item->item;
        asset_buffer->buffer_alloc = buffer_alloc;
        asset_buffer->elem_size_in_bytes = buffer_info->type_size;
        asset_buffer->elem_count = buffer_info->elem_count;

        // ~mgj: copy to staging and record copy command
        VK_CHECK_RESULT(vmaCopyMemoryToAllocation(asset_manager->allocator, buffer_info->buffer.data, staging_buffer_alloc.allocation, 0, buffer_info->buffer.size));
        VkBufferCopy copy_region = {0};
        copy_region.size = buffer_info->buffer.size;
        vkCmdCopyBuffer((VkCommandBuffer)cmd, staging_buffer_alloc.buffer, asset_buffer->buffer_alloc.buffer, 1, &copy_region);

        asset_buffer->staging_buffer = staging_buffer_alloc;
    }
    else
    {
        buffer_destroy(&staging_buffer_alloc);
    }

    return asset_handle;
}

g_internal Handle
storage_buffer_load_sync(Arena* arena, Handle vertex_buffer_handle, Handle index_buffer_handle)
{
    Handle desc_handle = Handle();
    if (is_handle_zero(vertex_buffer_handle) == false && is_handle_zero(index_buffer_handle) == false)
    {
        AssetItem<vulkan::BufferHandle>* vertex_asset = vulkan::asset_manager_buffer_item_get(vertex_buffer_handle);
        AssetItem<vulkan::BufferHandle>* index_asset = vulkan::asset_manager_buffer_item_get(index_buffer_handle);
        if (vertex_asset && index_asset)
        {
            vulkan::StorageBufferDescriptor* storage_desc = PushStruct(arena, vulkan::StorageBufferDescriptor);
            storage_desc->vertex_buffer = vertex_asset->item.buffer_alloc.buffer;
            storage_desc->index_buffer = index_asset->item.buffer_alloc.buffer;
        }
    }
    else
    {
        AssertStr("storage_buffer_load_sync: zero handle passed");
    }

    return desc_handle;
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
    U32 type = buffer_info->buffer_type;
    if (type & render::BufferType_Vertex)
        usage_flags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (type & render::BufferType_Index)
        usage_flags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (type & render::BufferType_Uniform)
        usage_flags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if (type & render::BufferType_StorageBuffer)
        usage_flags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    Assert(usage_flags != 0);
    vulkan::BufferAllocation buffer = vulkan::buffer_allocation_create(buffer_info->buffer.size, usage_flags | VK_BUFFER_USAGE_TRANSFER_DST_BIT, vma_info, "buffer_load_async");

    // ~mgj: Prepare buffer asset item
    OS_MutexScopeW(asset_manager->buffer_mutex)
    {
        AssetItem<vulkan::BufferHandle>* asset_item = (AssetItem<vulkan::BufferHandle>*)asset_handle.ptr;
        vulkan::BufferHandle* asset_buffer = (vulkan::BufferHandle*)&asset_item->item;
        asset_buffer->buffer_alloc = buffer;
        asset_buffer->elem_size_in_bytes = buffer_info->type_size;
        asset_buffer->elem_count = buffer_info->elem_count;
    }

    // ~mgj: Preparing buffer loading for another thread
    render::ThreadWorkerCmdCtx* thread_input = render::thread_input_create();
    render::handle_list_push(thread_input->arena, &thread_input->handles, asset_handle);

    // Copy buffer_info to thread_input arena to ensure it persists for async execution
    render::BufferInfo* buffer_info_copy = PushStruct(thread_input->arena, render::BufferInfo);
    *buffer_info_copy = *buffer_info;
    buffer_info_copy->buffer = buffer_alloc<U8>(thread_input->arena, buffer_info->buffer.size);
    MemoryCopy(buffer_info_copy->buffer.data, buffer_info->buffer.data, buffer_info->buffer.size);

    thread_input->user_data = buffer_info_copy;
    thread_input->loading_func = vulkan::buffer_loading_thread;
    thread_input->done_loading_func = render::handle_done_loading;

    async::thread_pool_push(thread_input->arena, thread_input, vulkan::thread_main, asset_manager->threads);

    return asset_handle;
}

g_internal void
texture_gpu_upload_sync(render::Handle tex_handle, Buffer<U8> tex_buf)
{
    vulkan::Context* vk_ctx = vulkan::ctx_get();
    VkResult result;

    VkCommandBufferAllocateInfo cmd_buf_alloc_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = vk_ctx->command_pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
    VkCommandBuffer cmd_buf;
    result = vkAllocateCommandBuffers(vk_ctx->device, &cmd_buf_alloc_info, &cmd_buf);
    VkCommandBufferBeginInfo begin_info = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    result = vkBeginCommandBuffer(cmd_buf, &begin_info);
    vulkan::texture_gpu_upload_cmd_recording(cmd_buf, tex_handle, tex_buf);

    result = vkEndCommandBuffer(cmd_buf);

    VkCommandBufferSubmitInfo cmd_buf_info{};
    cmd_buf_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmd_buf_info.commandBuffer = cmd_buf;

    VkSubmitInfo2 submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submit_info.commandBufferInfoCount = 1;
    submit_info.pCommandBufferInfos = &cmd_buf_info;

    render::AssetItem<vulkan::TextureHandle>* asset = (render::AssetItem<vulkan::TextureHandle>*)tex_handle.ptr;
    asset->is_loaded = true;
    result = vkQueueSubmit2(vk_ctx->graphics_queue, 1, &submit_info, NULL);
}

g_internal void
model_3d_draw(Model3DPipelineData pipeline_input, render::Handle colormap_handle)
{
    render::AssetItem<vulkan::BufferHandle>* asset_vertex_buffer = 0;
    render::AssetItem<vulkan::BufferHandle>* asset_index_buffer = 0;
    render::AssetItem<vulkan::TextureHandle>* asset_base_texture = 0;
    render::AssetItem<vulkan::TextureHandle>* asset_colormap = 0;

    B32 vertex_loaded = render::is_resource_loaded(pipeline_input.vertex_buffer_handle, &asset_vertex_buffer);
    B32 index_loaded = render::is_resource_loaded(pipeline_input.index_buffer_handle, &asset_index_buffer);
    B32 base_texture_loaded = render::is_resource_loaded(pipeline_input.texture_handle, &asset_base_texture);
    B32 colormap_loaded = render::is_resource_loaded(colormap_handle, &asset_colormap);
    if (vertex_loaded && index_loaded && base_texture_loaded && colormap_loaded)
    {
        render::Handle overlay_texture_handle = render::texture_zero_handle_get();
        B32 overlay_enabled = false;
        B32 overlay_loaded = false;
        render::AssetItem<vulkan::TextureHandle>* asset_overlay_texture = 0;
        if (pipeline_input.has_overlay_uv && pipeline_input.overlay_texture_coordinate_id == 0 && render::is_handle_zero(pipeline_input.overlay_texture_handle) == false)
        {
            overlay_loaded = render::is_resource_loaded(pipeline_input.overlay_texture_handle, &asset_overlay_texture);
            if (overlay_loaded)
            {
                overlay_texture_handle = pipeline_input.overlay_texture_handle;
                overlay_enabled = true;
            }
        }

        Rng2F32 bbox = {pipeline_input.bbox_min, pipeline_input.bbox_max};
        vulkan::model_3d_bucket_add(&asset_vertex_buffer->item.buffer_alloc, &asset_index_buffer->item.buffer_alloc, pipeline_input.texture_handle, overlay_texture_handle, overlay_enabled,
                                    pipeline_input.overlay_translation, pipeline_input.overlay_scale, false, pipeline_input.index_offset, pipeline_input.index_count,
                                    asset_colormap->item.descriptor_set_idx, bbox);
    }
    else
    {
        DEBUG_LOG("\x1b[31mmodel_3d_draw skipped: vertex=%d(%llu) index=%d(%llu) base_tex=%d(%llu) colormap=%d(%llu) has_overlay_uv=%d overlay_coord=%d overlay_handle=%llu overlay_loaded=%d\x1b[0m",
                  vertex_loaded, pipeline_input.vertex_buffer_handle.u64, index_loaded, pipeline_input.index_buffer_handle.u64, base_texture_loaded, pipeline_input.texture_handle.u64, colormap_loaded,
                  colormap_handle.u64, pipeline_input.has_overlay_uv, pipeline_input.overlay_texture_coordinate_id, pipeline_input.overlay_texture_handle.u64,
                  render::is_handle_zero(pipeline_input.overlay_texture_handle) ? 0 : render::is_resource_loaded(pipeline_input.overlay_texture_handle));
    }
}

g_internal void
car_instance_compute_bucket_add(render::BufferInfo* instance_buffer_info, render::Handle tile_vertex_buffer_handle, render::Handle tile_index_buffer_handle, F32 car_center_to_road_offset,
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
        vulkan::CarHeightCalculatePushConstants compute_push_constants = {.car_count = instance_buffer_info->elem_count, .car_center_offset = car_center_to_road_offset};
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
car_instance_render_bucket_add(render::Handle vertex_buffer_handle, render::Handle index_buffer_handle, render::Handle tex_handle, render::BufferInfo* instance_buffer_info, U32 instance_buffer_offset)
{
    if (instance_buffer_info->buffer.size == 0 || instance_buffer_info->elem_count == 0)
    {
        return false;
    }

    vulkan::Context* vk_ctx = vulkan::ctx_get();
    vulkan::RenderFrame* render_frame = vk_ctx->render_frame;
    vulkan::CarInstanceRender* instance_draw = &render_frame->car_instance_render_list;

    render::AssetItem<vulkan::TextureHandle>* asset_tex = 0;
    render::AssetItem<vulkan::BufferHandle>* asset_vertex = 0;
    render::AssetItem<vulkan::BufferHandle>* asset_index = 0;
    if (render::is_resource_loaded(tex_handle, &asset_tex) && render::is_resource_loaded(vertex_buffer_handle, &asset_vertex) && render::is_resource_loaded(index_buffer_handle, &asset_index))
    {
        vulkan::TextureHandle* tex = &asset_tex->item;

        // draw ressources
        vulkan::CarInstancePushConstants push_constants = {.tex_idx = tex->descriptor_set_idx};
        vulkan::CarInstanceRenderNode* node = PushStruct(vk_ctx->render_frame_arena, vulkan::CarInstanceRenderNode);
        node->vertex_handle = &asset_vertex->item;
        node->index_handle = &asset_index->item;
        node->draw_push_constants = push_constants;
        node->instance_buffer_info = *instance_buffer_info;
        node->instance_buffer_offset = instance_buffer_offset;
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

g_internal void
blend_3d_draw(render::Blend3DPipelineData pipeline_input)
{
    render::AssetItem<vulkan::BufferHandle>* asset_vertex_buffer = 0;
    render::AssetItem<vulkan::BufferHandle>* asset_index_buffer = 0;
    render::AssetItem<vulkan::TextureHandle>* asset_texture = 0;
    render::AssetItem<vulkan::TextureHandle>* asset_colormap = 0;
    if (render::is_resource_loaded(pipeline_input.index_buffer_handle, &asset_index_buffer) && render::is_resource_loaded(pipeline_input.vertex_buffer_handle, &asset_vertex_buffer) &&
        render::is_resource_loaded(pipeline_input.texture_handle, &asset_texture) && render::is_resource_loaded(pipeline_input.colormap_handle, &asset_colormap))
    {
        vulkan::blend_3d_bucket_add(&asset_vertex_buffer->item.buffer_alloc, &asset_index_buffer->item.buffer_alloc, pipeline_input.texture_handle, pipeline_input.colormap_handle);
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
    OS_RWMutexTakeW(asset_manager->texture_mutex);
    Handle handle = asset_manager_item_create(&asset_manager->texture_list, &asset_manager->texture_free_list, render::HandleType::Texture);
    OS_RWMutexDropW(asset_manager->texture_mutex);
    return handle;
}

Handle
Handle::buffer_handle_create(BufferType buffer_type)
{
    vulkan::AssetManager* asset_manager = vulkan::asset_manager_get();
    OS_RWMutexTakeW(asset_manager->buffer_mutex);
    Handle handle = asset_manager_item_create(&asset_manager->buffer_list, &asset_manager->buffer_free_list, render::HandleType::Buffer);
    render::AssetItem<vulkan::BufferHandle>* asset_item = (render::AssetItem<vulkan::BufferHandle>*)handle.ptr;
    asset_item->item.type = buffer_type;

    OS_RWMutexDropW(asset_manager->buffer_mutex);
    return handle;
}

g_internal void
thread_cmd_buffer_record(ThreadWorkerCmdCtx* thread_ctx)
{
    vulkan::AssetManager* asset_manager = vulkan::asset_manager_get();

    U32 thread_local_id = async::t_cur_thread_id;
    // ~mgj: Record the command buffer
    thread_ctx->cmd_buffer = begin_command(asset_manager->device, &asset_manager->threaded_cmd_pools.data[thread_local_id]);
}

g_internal void
thread_cmd_buffer_end(ThreadWorkerCmdCtx* cmd_ctx)
{
    U32 thread_local_id = async::t_cur_thread_id;
    VK_CHECK_RESULT(vkEndCommandBuffer((VkCommandBuffer)cmd_ctx->cmd_buffer));
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

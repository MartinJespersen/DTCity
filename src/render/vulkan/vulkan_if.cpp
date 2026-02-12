namespace render
{

// ~mgj: Vulkan Interface
static void
render_ctx_create(String8 shader_path, io::IO* io_ctx, async::Threads* thread_pool)
{
    ScratchScope scratch = ScratchScope(0, 0);

    Arena* arena = arena_alloc();

    vulkan::Context* vk_ctx = PushStruct(arena, vulkan::Context);
    vulkan::ctx_set(vk_ctx);
    vk_ctx->arena = arena;
    vk_ctx->render_thread_id = os_tid();

    const char* validation_layers[] = {"VK_LAYER_KHRONOS_validation",
                                       "VK_LAYER_KHRONOS_synchronization2"};
    vk_ctx->validation_layers = BufferAlloc<String8>(vk_ctx->arena, ArrayCount(validation_layers));
    for (U32 i = 0; i < ArrayCount(validation_layers); i++)
    {
        vk_ctx->validation_layers.data[i] = {str8_c_string(validation_layers[i])};
    }

    const char* device_extensions[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
        VK_EXT_COLOR_WRITE_ENABLE_EXTENSION_NAME, VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME};
    vk_ctx->device_extensions = BufferAlloc<String8>(vk_ctx->arena, ArrayCount(device_extensions));
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
    vkGetPhysicalDeviceFormatProperties(vk_ctx->physical_device, vk_ctx->blit_format,
                                        &formatProperties);
    if (!(formatProperties.optimalTilingFeatures &
          VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))
    {
        exit_with_error("texture image format does not support linear blitting!");
    }

    vk_ctx->object_id_format = VK_FORMAT_R32G32_UINT;

    // ~mgj: Create asset manager (includes VMA allocator)
    vk_ctx->asset_manager = vulkan::asset_manager_create(
        vk_ctx->physical_device, vk_ctx->device, vk_ctx->instance, vk_ctx->graphics_queue,
        vk_ctx->queue_family_indices.graphicsFamilyIndex, thread_pool, GB(1));

    Vec2S32 vk_framebuffer_dim_s32 = io::wait_for_valid_framebuffer_size(io_ctx);
    Vec2U32 vk_framebuffer_dim_u32 = {(U32)vk_framebuffer_dim_s32.x, (U32)vk_framebuffer_dim_s32.y};
    vulkan::SwapChainSupportDetails swapchain_details =
        vulkan::query_swapchain_support(scratch.arena, vk_ctx->physical_device, vk_ctx->surface);
    VkExtent2D swapchain_extent =
        vulkan::choose_swap_extent(vk_framebuffer_dim_u32, swapchain_details.capabilities);
    vk_ctx->swapchain_resources =
        vulkan::swapchain_create(vk_ctx, &swapchain_details, swapchain_extent);

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = vk_ctx->queue_family_indices.graphicsFamilyIndex;
    vk_ctx->command_pool = vulkan::command_pool_create(vk_ctx->device, &poolInfo);

    vulkan::command_buffers_create(vk_ctx);

    vulkan::sync_objects_create(vk_ctx);

    vk_ctx->max_texture_count = 5000;
    vulkan::descriptor_pool_create(vk_ctx, vk_ctx->max_texture_count);
    vulkan::camera_uniform_buffer_create(vk_ctx);
    vulkan::camera_descriptor_set_layout_create(vk_ctx);
    vulkan::camera_descriptor_set_create(vk_ctx);
    vulkan::profile_buffers_create(vk_ctx);

    // ~mgj: Drawing (TODO: Move out of vulkan context to own module)

    vk_ctx->texture_binding = 0;
    vk_ctx->texture_descriptor_set_layout = vulkan::descriptor_set_layout_create_bindless_textures(
        vk_ctx->device, vk_ctx->texture_binding, vk_ctx->max_texture_count,
        VK_SHADER_STAGE_FRAGMENT_BIT);
    vk_ctx->texture_descriptor_set = vulkan::descriptor_set_allocate_bindless(
        vk_ctx->device, vk_ctx->descriptor_pool, vk_ctx->texture_descriptor_set_layout,
        vk_ctx->max_texture_count);

    vk_ctx->draw_frame_arena = arena_alloc();
    vk_ctx->model_3D_pipeline = vulkan::model_3d_pipeline_create(vk_ctx, shader_path);
    vk_ctx->model_3D_instance_pipeline =
        vulkan::model_3d_instance_pipeline_create(vk_ctx, shader_path);
    vk_ctx->blend_3d_pipeline = vulkan::blend_3d_pipeline_create(shader_path);
}

static void
render_ctx_destroy()
{
    vulkan::Context* vk_ctx = vulkan::ctx_get();
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    vkDeviceWaitIdle(vk_ctx->device);

    if (vk_ctx->enable_validation_layers)
    {
        vulkan::destroy_debug_utils_messenger_ext(vk_ctx->instance, vk_ctx->debug_messenger,
                                                  nullptr);
    }

    vulkan::camera_cleanup(vk_ctx);
    vulkan::profile_buffers_destroy(vk_ctx);

    // Asset manager must be destroyed first — it flushes pending GPU commands
    // that reference descriptor sets, pipelines, and other Vulkan resources
    vulkan::buffer_destroy(&vk_ctx->model_3D_instance_buffer);

    vulkan::swapchain_cleanup(vk_ctx->device, vk_ctx->swapchain_resources);
    vulkan::asset_manager_destroy(vk_ctx->asset_manager);
    vkDestroyCommandPool(vk_ctx->device, vk_ctx->command_pool, nullptr);

    vkDestroySurfaceKHR(vk_ctx->instance, vk_ctx->surface, nullptr);
    for (U32 i = 0; i < vulkan::MAX_FRAMES_IN_FLIGHT; i++)
    {
        vkDestroyFence(vk_ctx->device, vk_ctx->in_flight_fences.data[i], nullptr);
    }

    vkDestroyDescriptorPool(vk_ctx->device, vk_ctx->descriptor_pool, 0);

    vulkan::pipeline_destroy(&vk_ctx->model_3D_pipeline);
    vulkan::pipeline_destroy(&vk_ctx->model_3D_instance_pipeline);
    vulkan::pipeline_destroy(&vk_ctx->blend_3d_pipeline);

    vkDestroyDescriptorSetLayout(vk_ctx->device, vk_ctx->texture_descriptor_set_layout, nullptr);

    vkDestroyDevice(vk_ctx->device, nullptr);
    vkDestroyInstance(vk_ctx->instance, nullptr);

    arena_release(vk_ctx->draw_frame_arena);

    arena_release(vk_ctx->arena);
}

static void
render_frame(Vec2U32 framebuffer_dim, B32* in_out_framebuffer_resized, ui::Camera* camera,
             Vec2S64 mouse_cursor_pos)
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
        vulkan::SwapChainSupportDetails swapchain_details = vulkan::query_swapchain_support(
            scratch.arena, vk_ctx->physical_device, vk_ctx->surface);
        VkExtent2D swapchain_extent =
            vulkan::choose_swap_extent(framebuffer_dim, swapchain_details.capabilities);
        if (swapchain_extent.width != 0 && swapchain_extent.height != 0)
            vk_ctx->swapchain_resources =
                vulkan::swapchain_create(vk_ctx, &swapchain_details, swapchain_extent);
        return;
    }
    VkFence* in_flight_fence = &vk_ctx->in_flight_fences.data[vk_ctx->current_frame];
    U32 image_idx = 0;
    U32 prev_image_idx = vk_ctx->cur_img_idx;
    {
        VkSemaphore image_available_semaphore =
            swapchain_resources->image_available_semaphores.data[prev_image_idx];

        {
            prof_scope_marker_named("Wait for frame");
            VK_CHECK_RESULT(
                vkWaitForFences(vk_ctx->device, 1, in_flight_fence, VK_TRUE, 1000000000));
        }

        // Process deferred deletions after fence wait to ensure command buffers are done
        vulkan::deletion_queue_deferred_resource_deletion(vk_ctx->asset_manager->deletion_queue);

        VkResult result = vkAcquireNextImageKHR(
            vk_ctx->device, vk_ctx->swapchain_resources->swapchain, UINT64_MAX,
            image_available_semaphore, VK_NULL_HANDLE, &image_idx);
        vk_ctx->cur_img_idx = (prev_image_idx + 1) % swapchain_resources->image_count;

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

    VkSemaphore render_finished_semaphore =
        swapchain_resources->render_finished_semaphores.data[prev_image_idx];
    VkSemaphore image_available_semaphore =
        swapchain_resources->image_available_semaphores.data[prev_image_idx];

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
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR ||
        *in_out_framebuffer_resized)
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
    arena_clear(vk_ctx->draw_frame_arena);
    vk_ctx->draw_frame = PushStruct(vk_ctx->draw_frame_arena, vulkan::DrawFrame);
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
        AssetItem<vulkan::Texture>* asset_item = (AssetItem<vulkan::Texture>*)asset_handle.ptr;
        vulkan::Texture* texture = &asset_item->item;
        texture->descriptor_set_idx =
            vulkan::descriptor_index_allocate(&asset_manager->descriptor_index_allocator);
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
    render::ThreadInput* thread_input = render::thread_input_create();

    // ~mgj: make input ready for texture loading on thread
    render::TextureLoadingInfo* texture_load_info =
        PushStruct(thread_input->arena, render::TextureLoadingInfo);
    texture_load_info->tex_path = push_str8_copy(thread_input->arena, texture_path);
    thread_input->user_data = texture_load_info;
    render::handle_list_push(thread_input->arena, &thread_input->handles,
                             render::texture_handle_create(sampler_info));

    thread_input->loading_func = vulkan::texture_loading_from_path_thread;
    thread_input->done_loading_func = render::handle_done_loading;

    async::QueueItem queue_input = {.data = thread_input, .worker_func = vulkan::thread_main};
    async::QueuePush(asset_manager->work_queue, &queue_input);

    return render::handle_list_first_handle(&thread_input->handles);
}

g_internal Handle
texture_load_sync(render::SamplerInfo* sampler_info, TextureUploadData* tex_data,
                  VkCommandBuffer cmd)
{
    prof_scope_marker;
    ScratchScope scratch = ScratchScope(0, 0);

    // ~mgj: make input ready for texture loading on thread
    render::Handle handle = render::texture_handle_create(sampler_info);

    {
        Assert(tex_data);
        vulkan::ImageAllocationResource image_allocation_resource =
            vulkan::texture_upload_with_blitting(cmd, tex_data);

        render::AssetItem<vulkan::Texture>* tex_asset =
            vulkan::asset_manager_item_get<vulkan::Texture>(handle);
        if (tex_asset)
        {
            tex_asset->item.image_resource = image_allocation_resource.image_resource;
            tex_asset->item.staging_allocation = image_allocation_resource.staging_buffer_alloc;
        }
        else
        {
            buffer_destroy(&image_allocation_resource.staging_buffer_alloc);
            image_resource_destroy(image_allocation_resource.image_resource);
            DEBUG_LOG("texture_loading_thread: Asset Item: %llu - Not Found", handle.u64);
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
    render::ThreadInput* thread_input = render::thread_input_create();

    // ~mgj: make input ready for texture loading on thread
    TextureUploadData* tex_upload_data = PushStruct(thread_input->arena, TextureUploadData);
    *tex_upload_data = *tex_upload_info;
    tex_upload_data->data = PushArray(thread_input->arena, U8, tex_upload_info->data_byte_size);
    MemoryCopy(tex_upload_data->data, tex_upload_info->data, tex_upload_info->data_byte_size);

    thread_input->user_data = tex_upload_data;
    render::Handle handle = render::texture_handle_create(sampler_info);

    thread_input->loading_func = vulkan::texture_loading_thread;
    thread_input->done_loading_func = render::handle_done_loading;

    async::QueueItem queue_input = {.data = thread_input, .worker_func = vulkan::thread_main};
    async::QueuePush(asset_manager->work_queue, &queue_input);

    return handle;
}

static render::Handle
colormap_load_async(render::SamplerInfo* sampler_info, const U8* colormap_data, U64 colormap_size)
{
    vulkan::Context* vk_ctx = vulkan::ctx_get();
    vulkan::AssetManager* asset_manager = vk_ctx->asset_manager;
    render::ThreadInput* thread_input = render::thread_input_create();

    // ~mgj: make input ready for colormap loading on thread
    render::ColorMapLoadingInfo* colormap_load_info =
        PushStruct(thread_input->arena, render::ColorMapLoadingInfo);
    colormap_load_info->colormap_data = colormap_data;
    colormap_load_info->colormap_size = colormap_size;

    render::handle_list_push(thread_input->arena, &thread_input->handles,
                             render::texture_handle_create(sampler_info));
    thread_input->user_data = colormap_load_info;
    thread_input->loading_func = vulkan::colormap_loading_thread;
    thread_input->done_loading_func = render::handle_done_loading;

    async::QueueItem queue_input = {.data = thread_input, .worker_func = vulkan::thread_main};
    async::QueuePush(asset_manager->work_queue, &queue_input);

    return render::handle_list_first_handle(&thread_input->handles);
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
        vulkan::Context* vk_ctx = vulkan::ctx_get();
        vulkan::deletion_queue_push(vk_ctx->asset_manager->deletion_queue, handle,
                                    vulkan::MAX_FRAMES_IN_FLIGHT);
    }
}

g_internal Handle
buffer_load_sync(VkCommandBuffer cmd, render::BufferInfo* buffer_info)
{
    prof_scope_marker;
    if (buffer_info->buffer.size == 0)
    {
        DEBUG_LOG("Zero handle created for buffer\n");
        return render::handle_zero();
    }

    vulkan::Context* vk_ctx = vulkan::ctx_get();
    vulkan::AssetManager* asset_manager = vk_ctx->asset_manager;
    render::Handle asset_handle = render::Handle::buffer_handle_create();

    // ~mgj: Create buffer allocation
    VmaAllocationCreateInfo vma_info = {0};
    vma_info.usage = VMA_MEMORY_USAGE_AUTO;
    vma_info.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    VkBufferUsageFlags usage_flags = {};
    switch (buffer_info->buffer_type)
    {
        case render::BufferType_Vertex: usage_flags = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT; break;
        case render::BufferType_Index: usage_flags = VK_BUFFER_USAGE_INDEX_BUFFER_BIT; break;
        default: InvalidPath;
    }
    vulkan::BufferAllocation buffer_alloc = vulkan::buffer_allocation_create(
        buffer_info->buffer.size, usage_flags | VK_BUFFER_USAGE_TRANSFER_DST_BIT, vma_info);

    // ~mgj: Create staging buffer allocation
    vulkan::BufferAllocation staging_buffer_alloc =
        vulkan::staging_buffer_create(buffer_info->buffer.size);

    // ~mgj: Prepare buffer asset item
    AssetItem<vulkan::BufferUpload>* asset_item =
        (AssetItem<vulkan::BufferUpload>*)asset_handle.ptr;
    if (asset_item)
    {
        vulkan::BufferUpload* asset_buffer = (vulkan::BufferUpload*)&asset_item->item;
        asset_buffer->buffer_alloc = buffer_alloc;

        // ~mgj: copy to staging and record copy command
        VK_CHECK_RESULT(vmaCopyMemoryToAllocation(
            asset_manager->allocator, buffer_info->buffer.data, staging_buffer_alloc.allocation, 0,
            buffer_info->buffer.size));
        VkBufferCopy copy_region = {0};
        copy_region.size = buffer_info->buffer.size;
        vkCmdCopyBuffer(cmd, staging_buffer_alloc.buffer, asset_buffer->buffer_alloc.buffer, 1,
                        &copy_region);

        asset_buffer->staging_buffer = staging_buffer_alloc;
    }
    else
    {
        DEBUG_LOG("buffer_loading_thread: Asset Item: %llu - Not Found", asset_handle.u64);
        buffer_destroy(&staging_buffer_alloc);
    }

    return asset_handle;
}

static render::Handle
buffer_load(render::BufferInfo* buffer_info)
{
    prof_scope_marker;
    if (buffer_info->buffer.size == 0)
    {
        DEBUG_LOG("Zero handle created for buffer\n");
        return render::handle_zero();
    }

    vulkan::Context* vk_ctx = vulkan::ctx_get();
    vulkan::AssetManager* asset_manager = vk_ctx->asset_manager;
    render::Handle asset_handle = render::Handle::buffer_handle_create();

    // ~mgj: Create buffer allocation
    VmaAllocationCreateInfo vma_info = {0};
    vma_info.usage = VMA_MEMORY_USAGE_AUTO;
    vma_info.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    VkBufferUsageFlags usage_flags = {};
    switch (buffer_info->buffer_type)
    {
        case render::BufferType_Vertex: usage_flags = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT; break;
        case render::BufferType_Index: usage_flags = VK_BUFFER_USAGE_INDEX_BUFFER_BIT; break;
        default: InvalidPath;
    }
    vulkan::BufferAllocation buffer = vulkan::buffer_allocation_create(
        buffer_info->buffer.size, usage_flags | VK_BUFFER_USAGE_TRANSFER_DST_BIT, vma_info);

    // ~mgj: Prepare buffer asset item
    OS_MutexScopeW(asset_manager->buffer_mutex)
    {
        AssetItem<vulkan::BufferUpload>* asset_item =
            (AssetItem<vulkan::BufferUpload>*)asset_handle.ptr;
        vulkan::BufferUpload* asset_buffer = (vulkan::BufferUpload*)&asset_item->item;
        asset_buffer->buffer_alloc = buffer;
    }

    // ~mgj: Preparing buffer loading for another thread
    render::ThreadInput* thread_input = render::thread_input_create();
    render::handle_list_push(thread_input->arena, &thread_input->handles, asset_handle);

    // Copy buffer_info to thread_input arena to ensure it persists for async execution
    render::BufferInfo* buffer_info_copy = PushStruct(thread_input->arena, render::BufferInfo);
    *buffer_info_copy = *buffer_info;
    buffer_info_copy->buffer = BufferAlloc<U8>(thread_input->arena, buffer_info->buffer.size);
    MemoryCopy(buffer_info_copy->buffer.data, buffer_info->buffer.data, buffer_info->buffer.size);

    thread_input->user_data = buffer_info_copy;
    thread_input->loading_func = vulkan::buffer_loading_thread;
    thread_input->done_loading_func = render::handle_done_loading;

    async::QueueItem queue_input = {.data = thread_input, .worker_func = vulkan::thread_main};
    async::QueuePush(asset_manager->work_queue, &queue_input);

    return asset_handle;
}

g_internal void
texture_gpu_upload_sync(render::Handle tex_handle, Buffer<U8> tex_buf)
{
    vulkan::Context* vk_ctx = vulkan::ctx_get();
    VkResult result;

    VkCommandBufferAllocateInfo cmd_buf_alloc_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = vk_ctx->command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1};
    VkCommandBuffer cmd_buf;
    result = vkAllocateCommandBuffers(vk_ctx->device, &cmd_buf_alloc_info, &cmd_buf);
    VkCommandBufferBeginInfo begin_info = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                           .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
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

    render::AssetItem<vulkan::Texture>* asset = (render::AssetItem<vulkan::Texture>*)tex_handle.ptr;
    asset->is_loaded = true;
    result = vkQueueSubmit2(vk_ctx->graphics_queue, 1, &submit_info, NULL);
}

g_internal void
model_3d_instance_draw(render::Handle texture_handle, render::Handle vertex_buffer_handle,
                       render::Handle index_buffer_handle, render::BufferInfo* instance_buffer)
{
    render::AssetItem<vulkan::BufferUpload>* asset_vertex_buffer =
        (render::AssetItem<vulkan::BufferUpload>*)(vertex_buffer_handle.ptr);
    render::AssetItem<vulkan::BufferUpload>* asset_index_buffer =
        (render::AssetItem<vulkan::BufferUpload>*)(index_buffer_handle.ptr);
    render::AssetItem<vulkan::Texture>* asset_texture =
        (render::AssetItem<vulkan::Texture>*)(texture_handle.ptr);

    if (asset_vertex_buffer->is_loaded && asset_index_buffer->is_loaded && asset_texture->is_loaded)
    {
        vulkan::model_3d_instance_bucket_add(&asset_vertex_buffer->item.buffer_alloc,
                                             &asset_index_buffer->item.buffer_alloc, texture_handle,
                                             instance_buffer);
    }
}

g_internal void
model_3d_draw(render::Model3DPipelineData pipeline_input)
{
    if (render::is_handle_zero(pipeline_input.index_buffer_handle) ||
        render::is_handle_zero(pipeline_input.vertex_buffer_handle) ||
        render::is_handle_zero(pipeline_input.texture_handle))
        return;

    if (render::is_resource_loaded(pipeline_input.vertex_buffer_handle) &&
        render::is_resource_loaded(pipeline_input.index_buffer_handle) &&
        render::is_resource_loaded(pipeline_input.texture_handle))
    {
        render::AssetItem<vulkan::BufferUpload>* asset_vertex_buffer =
            vulkan::asset_manager_buffer_item_get(pipeline_input.vertex_buffer_handle);
        render::AssetItem<vulkan::BufferUpload>* asset_index_buffer =
            vulkan::asset_manager_buffer_item_get(pipeline_input.index_buffer_handle);
        vulkan::model_3d_bucket_add(&asset_vertex_buffer->item.buffer_alloc,
                                    &asset_index_buffer->item.buffer_alloc,
                                    pipeline_input.texture_handle, false,
                                    pipeline_input.index_offset, pipeline_input.index_count);
    }
}

g_internal void
blend_3d_draw(render::Blend3DPipelineData pipeline_input)
{
    if (render::is_handle_zero(pipeline_input.index_buffer_handle) ||
        render::is_handle_zero(pipeline_input.vertex_buffer_handle) ||
        render::is_handle_zero(pipeline_input.texture_handle) ||
        render::is_handle_zero(pipeline_input.colormap_handle))
        return;
    render::AssetItem<vulkan::BufferUpload>* asset_vertex_buffer =
        (render::AssetItem<vulkan::BufferUpload>*)(pipeline_input.vertex_buffer_handle.ptr);
    render::AssetItem<vulkan::BufferUpload>* asset_index_buffer =
        (render::AssetItem<vulkan::BufferUpload>*)(pipeline_input.index_buffer_handle.ptr);
    render::AssetItem<vulkan::Texture>* asset_texture =
        (render::AssetItem<vulkan::Texture>*)(pipeline_input.texture_handle.ptr);
    render::AssetItem<vulkan::Texture>* asset_colormap =
        (render::AssetItem<vulkan::Texture>*)(pipeline_input.colormap_handle.ptr);

    if (asset_vertex_buffer->is_loaded && asset_index_buffer->is_loaded &&
        asset_texture->is_loaded && asset_colormap->is_loaded)
    {
        vulkan::blend_3d_bucket_add(&asset_vertex_buffer->item.buffer_alloc,
                                    &asset_index_buffer->item.buffer_alloc,
                                    pipeline_input.texture_handle, pipeline_input.colormap_handle);
    }
}

g_internal bool
is_resource_loaded(render::Handle handle)
{
    // TODO: Refactor asset item to not be a template
    render::AssetItem<S64>* asset = (render::AssetItem<S64>*)handle.ptr;
    return asset->is_loaded;
}

Handle
Handle::texture_handle_create()
{
    vulkan::AssetManager* asset_manager = vulkan::asset_manager_get();
    OS_RWMutexTakeW(asset_manager->texture_mutex);
    Handle handle =
        asset_manager_item_create(&asset_manager->texture_list, &asset_manager->texture_free_list,
                                  render::HandleType::Texture);
    OS_RWMutexDropW(asset_manager->texture_mutex);
    return handle;
}

Handle
Handle::buffer_handle_create()
{
    vulkan::AssetManager* asset_manager = vulkan::asset_manager_get();
    OS_RWMutexTakeW(asset_manager->buffer_mutex);
    Handle handle = asset_manager_item_create(
        &asset_manager->buffer_list, &asset_manager->buffer_free_list, render::HandleType::Buffer);

    OS_RWMutexDropW(asset_manager->buffer_mutex);
    return handle;
}

static bool
is_handle_loaded(Handle handle)
{
    if (render::is_handle_zero(handle) == false)
    {
        render::AssetItem<S64>* asset = (render::AssetItem<S64>*)handle.ptr;
        return asset->is_loaded;
    }
    return false;
}

g_internal void*
thread_cmd_buffer_record(render::ThreadInput* thread_input, ThreadSyncCallback callback)
{
    vulkan::AssetManager* asset_manager = vulkan::asset_manager_get();

    U32 thread_local_id = async::t_cur_thread_id;
    // ~mgj: Record the command buffer
    thread_input->cmd_buffer = begin_command(
        asset_manager->device, &asset_manager->threaded_cmd_pools.data[thread_local_id]);

    void* result = callback.function(thread_input, callback.data);

    VK_CHECK_RESULT(vkEndCommandBuffer((VkCommandBuffer)thread_input->cmd_buffer));
    vulkan::asset_cmd_queue_item_enqueue(thread_local_id, thread_input);

    return result;
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
                render::AssetItem<vulkan::BufferUpload>* asset =
                    vulkan::asset_manager_buffer_item_get(handle);
                if (asset)
                {
                    buffer_destroy(&asset->item.staging_buffer);
                    asset->item.staging_buffer.buffer = 0;
                    asset->is_loaded = 1;
                }
            }
            break;
            case render::HandleType::Texture:
            {
                render::AssetItem<vulkan::Texture>* asset =
                    vulkan::asset_manager_texture_item_get(handle);
                if (asset)
                {
                    vulkan::Texture* texture = &asset->item;
                    vulkan::descriptor_set_update_bindless_texture(
                        texture->descriptor_set_idx,
                        texture->image_resource.image_view_resource.image_view, texture->sampler);
                    buffer_destroy(&asset->item.staging_allocation);
                    asset->is_loaded = 1;
                }
            }
            break;
            default: InvalidPath; break;
        }
    }
}
} // namespace render

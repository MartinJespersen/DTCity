namespace render
{

// ~mgj: Vulkan Interface
static void
render_ctx_create(String8 shader_path, io::IO* io_ctx, async::Threads* thread_pool)
{
    ScratchScope scratch = ScratchScope(0, 0);

    Arena* arena = ArenaAlloc();

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

    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.physicalDevice = vk_ctx->physical_device;
    allocatorInfo.device = vk_ctx->device;
    allocatorInfo.instance = vk_ctx->instance;

    vmaCreateAllocator(&allocatorInfo, &vk_ctx->allocator);
    vk_ctx->object_id_format = VK_FORMAT_R32G32_UINT;

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

    vulkan::descriptor_pool_create(vk_ctx);
    vulkan::camera_uniform_buffer_create(vk_ctx);
    vulkan::camera_descriptor_set_layout_create(vk_ctx);
    vulkan::camera_descriptor_set_create(vk_ctx);
    vulkan::profile_buffers_create(vk_ctx);

    // TODO: change from 1 to much larger value
    vk_ctx->asset_manager = vulkan::asset_manager_create(
        vk_ctx->device, vk_ctx->queue_family_indices.graphicsFamilyIndex, thread_pool, GB(1));

    // ~mgj: Drawing (TODO: Move out of vulkan context to own module)

    vk_ctx->max_texture_count = 10;
    vk_ctx->texture_binding = 0;
    vk_ctx->texture_descriptor_set_layout = vulkan::descriptor_set_layout_create_bindless_textures(
        vk_ctx->device, vk_ctx->texture_binding, vk_ctx->max_texture_count,
        VK_SHADER_STAGE_FRAGMENT_BIT);
    vk_ctx->texture_descriptor_set = vulkan::descriptor_set_allocate_bindless(
        vk_ctx->device, vk_ctx->descriptor_pool, vk_ctx->texture_descriptor_set_layout,
        vk_ctx->max_texture_count);

    vk_ctx->draw_frame_arena = ArenaAlloc();
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

    vulkan::swapchain_cleanup(vk_ctx->device, vk_ctx->allocator, vk_ctx->swapchain_resources);

    vkDestroyCommandPool(vk_ctx->device, vk_ctx->command_pool, nullptr);

    vkDestroySurfaceKHR(vk_ctx->instance, vk_ctx->surface, nullptr);
    for (U32 i = 0; i < vulkan::MAX_FRAMES_IN_FLIGHT; i++)
    {
        vkDestroyFence(vk_ctx->device, vk_ctx->in_flight_fences.data[i], nullptr);
    }

    vkDestroyDescriptorPool(vk_ctx->device, vk_ctx->descriptor_pool, 0);

    vulkan::buffer_destroy(vk_ctx->allocator, &vk_ctx->model_3D_instance_buffer);

    vulkan::asset_manager_destroy(vk_ctx, vk_ctx->asset_manager);

    vulkan::pipeline_destroy(&vk_ctx->model_3D_pipeline);
    vulkan::pipeline_destroy(&vk_ctx->model_3D_instance_pipeline);
    vulkan::pipeline_destroy(&vk_ctx->blend_3d_pipeline);

    vkDestroyDescriptorSetLayout(vk_ctx->device, vk_ctx->texture_descriptor_set_layout, nullptr);

    vmaDestroyAllocator(vk_ctx->allocator);

    vkDestroyDevice(vk_ctx->device, nullptr);
    vkDestroyInstance(vk_ctx->instance, nullptr);

    ArenaRelease(vk_ctx->draw_frame_arena);

    ArenaRelease(vk_ctx->arena);
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
    vulkan::deletion_queue_deferred_resource_deletion(vk_ctx->asset_manager->deletion_queue);

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
    ArenaClear(vk_ctx->draw_frame_arena);
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
    U32 out_idx;
    render::AssetItem<vulkan::Texture>* asset_item = vulkan::asset_manager_item_create(
        &asset_manager->texture_list, &asset_manager->texture_free_list, &out_idx);
    vulkan::Texture* texture = &asset_item->item;
    texture->sampler = vk_sampler;
    texture->descriptor_set_idx = out_idx;
    render::Handle texture_handle = {.u64 = (U64)asset_item};

    return texture_handle;
}

static render::Handle
texture_load_async(render::SamplerInfo* sampler_info, String8 texture_path)
{
    ScratchScope scratch = ScratchScope(0, 0);
    vulkan::Context* vk_ctx = vulkan::ctx_get();
    vulkan::AssetManager* asset_manager = vk_ctx->asset_manager;
    vulkan::ThreadInput* thread_input = vulkan::thread_input_create();

    // ~mgj: make input ready for texture loading on thread
    render::TextureLoadingInfo* texture_load_info =
        PushStruct(thread_input->arena, render::TextureLoadingInfo);
    texture_load_info->tex_path = push_str8_copy(thread_input->arena, texture_path);
    thread_input->user_data = texture_load_info;
    thread_input->handle = render::texture_handle_create(sampler_info);

    thread_input->loading_func = vulkan::texture_loading_thread;
    thread_input->done_loading_func = vulkan::texture_done_loading;

    async::QueueItem queue_input = {.data = thread_input, .worker_func = vulkan::thread_main};
    async::QueuePush(asset_manager->work_queue, &queue_input);

    return thread_input->handle;
}

static render::Handle
colormap_load_async(render::SamplerInfo* sampler_info, const U8* colormap_data, U64 colormap_size)
{
    vulkan::Context* vk_ctx = vulkan::ctx_get();
    vulkan::AssetManager* asset_manager = vk_ctx->asset_manager;
    vulkan::ThreadInput* thread_input = vulkan::thread_input_create();

    // ~mgj: make input ready for colormap loading on thread
    render::ColorMapLoadingInfo* colormap_load_info =
        PushStruct(thread_input->arena, render::ColorMapLoadingInfo);
    colormap_load_info->colormap_data = colormap_data;
    colormap_load_info->colormap_size = colormap_size;

    thread_input->handle = render::texture_handle_create(sampler_info);
    thread_input->user_data = colormap_load_info;
    thread_input->loading_func = vulkan::colormap_loading_thread;
    thread_input->done_loading_func = vulkan::texture_done_loading;

    async::QueueItem queue_input = {.data = thread_input, .worker_func = vulkan::thread_main};
    async::QueuePush(asset_manager->work_queue, &queue_input);

    return thread_input->handle;
}

g_internal void
texture_destroy(render::Handle handle)
{
    texture_destroy_deferred(handle);
}

g_internal void
buffer_destroy(render::Handle handle)
{
    buffer_destroy_deferred(handle);
}

g_internal void
texture_destroy_deferred(render::Handle handle)
{
    if (render::is_handle_zero(handle) == false)
    {
        vulkan::Context* vk_ctx = vulkan::ctx_get();
        vulkan::deletion_queue_push(vk_ctx->asset_manager->deletion_queue, handle,
                                    render::AssetItemType_Texture, vulkan::MAX_FRAMES_IN_FLIGHT);
    }
}

g_internal void
buffer_destroy_deferred(render::Handle handle)
{
    if (render::is_handle_zero(handle) == false)
    {
        vulkan::Context* vk_ctx = vulkan::ctx_get();
        vulkan::deletion_queue_push(vk_ctx->asset_manager->deletion_queue, handle,
                                    render::AssetItemType_Buffer, vulkan::MAX_FRAMES_IN_FLIGHT);
    }
}

static render::Handle
buffer_load(render::BufferInfo* buffer_info)
{
    if (buffer_info->buffer.size == 0)
    {
        DEBUG_LOG("Zero handle created for buffer\n");
        return render::handle_zero();
    }

    vulkan::Context* vk_ctx = vulkan::ctx_get();
    vulkan::AssetManager* asset_manager = vk_ctx->asset_manager;
    U32 out_idx;
    render::AssetItem<vulkan::BufferUpload>* asset_item = vulkan::asset_manager_item_create(
        &asset_manager->buffer_list, &asset_manager->buffer_free_list, &out_idx);

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
    vulkan::BufferAllocation buffer =
        vulkan::buffer_allocation_create(vk_ctx->allocator, buffer_info->buffer.size,
                                         usage_flags | VK_BUFFER_USAGE_TRANSFER_DST_BIT, vma_info);

    // ~mgj: Prepare Texture
    vulkan::BufferUpload* asset_buffer = &asset_item->item;
    asset_buffer->buffer_alloc = buffer;
    render::Handle buffer_handle = {.u64 = (U64)asset_item};

    // ~mgj: Preparing buffer loading for another thread
    vulkan::ThreadInput* thread_input = vulkan::thread_input_create();
    thread_input->handle = buffer_handle;

    // Copy buffer_info to thread_input arena to ensure it persists for async execution
    render::BufferInfo* buffer_info_copy = PushStruct(thread_input->arena, render::BufferInfo);
    *buffer_info_copy = *buffer_info;

    thread_input->user_data = buffer_info_copy;
    thread_input->loading_func = vulkan::buffer_loading_thread;
    thread_input->done_loading_func = vulkan::buffer_done_loading_thread;

    async::QueueItem queue_input = {.data = thread_input, .worker_func = vulkan::thread_main};
    async::QueuePush(asset_manager->work_queue, &queue_input);

    render::Handle handle = {.u64 = (U64)asset_item};
    return handle;
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
    vulkan::Context* vk_ctx = vulkan::ctx_get();
    vulkan::AssetManager* asset_manager = vk_ctx->asset_manager;
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
model_3d_draw(render::Model3DPipelineData pipeline_input, B32 depth_test_per_draw_call_only)
{
    vulkan::Context* vk_ctx = vulkan::ctx_get();
    vulkan::AssetManager* asset_manager = vk_ctx->asset_manager;

    if (render::is_handle_zero(pipeline_input.index_buffer_handle) ||
        render::is_handle_zero(pipeline_input.vertex_buffer_handle) ||
        render::is_handle_zero(pipeline_input.texture_handle))
        return;
    render::AssetItem<vulkan::BufferUpload>* asset_vertex_buffer =
        (render::AssetItem<vulkan::BufferUpload>*)(pipeline_input.vertex_buffer_handle.ptr);
    render::AssetItem<vulkan::BufferUpload>* asset_index_buffer =
        (render::AssetItem<vulkan::BufferUpload>*)(pipeline_input.index_buffer_handle.ptr);

    if (asset_vertex_buffer->is_loaded && asset_index_buffer->is_loaded &&
        render::is_resource_loaded(pipeline_input.texture_handle))
    {
        vulkan::model_3d_bucket_add(&asset_vertex_buffer->item.buffer_alloc,
                                    &asset_index_buffer->item.buffer_alloc,
                                    pipeline_input.texture_handle, depth_test_per_draw_call_only,
                                    pipeline_input.index_offset, pipeline_input.index_count);
    }
}

g_internal void
blend_3d_draw(render::Blend3DPipelineData pipeline_input)
{
    vulkan::Context* vk_ctx = vulkan::ctx_get();
    vulkan::AssetManager* asset_manager = vk_ctx->asset_manager;

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
} // namespace render

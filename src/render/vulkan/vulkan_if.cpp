// ~mgj: Vulkan Interface
static void
r_render_ctx_create(String8 shader_path, io_IO* io_ctx, async::Threads* thread_pool)
{
    ScratchScope scratch = ScratchScope(0, 0);

    Arena* arena = ArenaAlloc();

    VK_Context* vk_ctx = PushStruct(arena, VK_Context);
    VK_CtxSet(vk_ctx);
    vk_ctx->arena = arena;
    vk_ctx->render_thread_id = os_tid();

    const char* validation_layers[] = {"VK_LAYER_KHRONOS_validation",
                                       "VK_LAYER_KHRONOS_synchronization2"};
    vk_ctx->validation_layers = BufferAlloc<String8>(vk_ctx->arena, ArrayCount(validation_layers));
    for (U32 i = 0; i < ArrayCount(validation_layers); i++)
    {
        vk_ctx->validation_layers.data[i] = {str8_c_string(validation_layers[i])};
    }

    const char* device_extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME,
                                       VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
                                       VK_EXT_COLOR_WRITE_ENABLE_EXTENSION_NAME};
    vk_ctx->device_extensions = BufferAlloc<String8>(vk_ctx->arena, ArrayCount(device_extensions));
    for (U32 i = 0; i < ArrayCount(device_extensions); i++)
    {
        vk_ctx->device_extensions.data[i] = {str8_c_string(device_extensions[i])};
    }

    VK_CreateInstance(vk_ctx);
    VK_DebugMessengerSetup(vk_ctx);
    VK_SurfaceCreate(vk_ctx, io_ctx);
    VK_PhysicalDevicePick(vk_ctx);
    VK_LogicalDeviceCreate(scratch.arena, vk_ctx);

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

    Vec2S32 vk_framebuffer_dim_s32 = io_wait_for_valid_framebuffer_size(io_ctx);
    Vec2U32 vk_framebuffer_dim_u32 = {(U32)vk_framebuffer_dim_s32.x, (U32)vk_framebuffer_dim_s32.y};
    VK_SwapChainSupportDetails swapchain_details =
        vk_query_swapchain_support(scratch.arena, vk_ctx->physical_device, vk_ctx->surface);
    VkExtent2D swapchain_extent =
        vk_choose_swap_extent(vk_framebuffer_dim_u32, swapchain_details.capabilities);
    vk_ctx->swapchain_resources = vk_swapchain_create(vk_ctx, &swapchain_details, swapchain_extent);

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = vk_ctx->queue_family_indices.graphicsFamilyIndex;
    vk_ctx->command_pool = VK_CommandPoolCreate(vk_ctx->device, &poolInfo);

    VK_CommandBuffersCreate(vk_ctx);

    vk_sync_objects_create(vk_ctx);

    VK_DescriptorPoolCreate(vk_ctx);
    VK_CameraUniformBufferCreate(vk_ctx);
    VK_CameraDescriptorSetLayoutCreate(vk_ctx);
    VK_CameraDescriptorSetCreate(vk_ctx);
    VK_ProfileBuffersCreate(vk_ctx);

    // TODO: change from 1 to much larger value
    vk_ctx->asset_manager = VK_AssetManagerCreate(
        vk_ctx->device, vk_ctx->queue_family_indices.graphicsFamilyIndex, thread_pool, GB(1));

    // ~mgj: Drawing (TODO: Move out of vulkan context to own module)
    vk_ctx->draw_frame_arena = ArenaAlloc();
    vk_ctx->model_3D_pipeline = VK_Model3DPipelineCreate(vk_ctx, shader_path);
    vk_ctx->model_3D_instance_pipeline = VK_Model3DInstancePipelineCreate(vk_ctx, shader_path);
}

static void
r_render_ctx_destroy()
{
    VK_Context* vk_ctx = VK_CtxGet();
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    vkDeviceWaitIdle(vk_ctx->device);

    if (vk_ctx->enable_validation_layers)
    {
        VK_DestroyDebugUtilsMessengerEXT(vk_ctx->instance, vk_ctx->debug_messenger, nullptr);
    }

    VK_CameraCleanup(vk_ctx);
    VK_ProfileBuffersDestroy(vk_ctx);

    vk_swapchain_cleanup(vk_ctx->device, vk_ctx->allocator, vk_ctx->swapchain_resources);

    vkDestroyCommandPool(vk_ctx->device, vk_ctx->command_pool, nullptr);

    vkDestroySurfaceKHR(vk_ctx->instance, vk_ctx->surface, nullptr);
    for (U32 i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; i++)
    {
        vkDestroyFence(vk_ctx->device, vk_ctx->in_flight_fences.data[i], nullptr);
    }

    vkDestroyDescriptorPool(vk_ctx->device, vk_ctx->descriptor_pool, 0);

    VK_BufferDestroy(vk_ctx->allocator, &vk_ctx->model_3D_instance_buffer);

    VK_AssetManagerDestroy(vk_ctx, vk_ctx->asset_manager);

    vmaDestroyAllocator(vk_ctx->allocator);

    VK_PipelineDestroy(&vk_ctx->model_3D_pipeline);
    VK_PipelineDestroy(&vk_ctx->model_3D_instance_pipeline);

    vkDestroyDevice(vk_ctx->device, nullptr);
    vkDestroyInstance(vk_ctx->instance, nullptr);

    ArenaRelease(vk_ctx->draw_frame_arena);

    ArenaRelease(vk_ctx->arena);
}

static void
r_render_frame(Vec2U32 framebuffer_dim, B32* in_out_framebuffer_resized, ui::Camera* camera,
               Vec2S64 mouse_cursor_pos)
{
    ScratchScope scratch = ScratchScope(0, 0);

    if (framebuffer_dim.x == 0 || framebuffer_dim.y == 0)
    {
        return;
    }
    VK_Context* vk_ctx = VK_CtxGet();
    defer({ vk_ctx->current_frame = (vk_ctx->current_frame + 1) % VK_MAX_FRAMES_IN_FLIGHT; });

    VK_AssetManagerExecuteCmds();
    VK_AssetManagerCmdDoneCheck();
    vk_deletion_queue_deferred_resource_deletion(vk_ctx->asset_manager->deletion_queue);

    vk_SwapchainResources* swapchain_resources = vk_ctx->swapchain_resources;
    if (!swapchain_resources)
    {
        VK_SwapChainSupportDetails swapchain_details =
            vk_query_swapchain_support(scratch.arena, vk_ctx->physical_device, vk_ctx->surface);
        VkExtent2D swapchain_extent =
            vk_choose_swap_extent(framebuffer_dim, swapchain_details.capabilities);
        if (swapchain_extent.width != 0 && swapchain_extent.height != 0)
            vk_ctx->swapchain_resources =
                vk_swapchain_create(vk_ctx, &swapchain_details, swapchain_extent);
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
            vk_swapchain_recreate(framebuffer_dim);
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

    vk_command_buffer_record(image_idx, vk_ctx->current_frame, camera, mouse_cursor_pos);

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
        vk_swapchain_recreate(framebuffer_dim);
    }
    else if (result != VK_SUCCESS)
    {
        exit_with_error("failed to present swap chain image!");
    }
}

static void
r_gpu_work_done_wait()
{
    VK_Context* vk_ctx = VK_CtxGet();
    vkDeviceWaitIdle(vk_ctx->device);
}

static void
r_new_frame()
{
    VK_Context* vk_ctx = VK_CtxGet();
    ArenaClear(vk_ctx->draw_frame_arena);
    vk_ctx->draw_frame = PushStruct(vk_ctx->draw_frame_arena, VK_DrawFrame);
    ImGui_ImplVulkan_NewFrame();
}

static U64
r_latest_hovered_object_id_get()
{
    VK_Context* vk_ctx = VK_CtxGet();
    return vk_ctx->hovered_object_id;
}

// ~mgj: Texture interface functions
g_internal r_Handle
r_texture_handle_create(r_SamplerInfo* sampler_info, r_PipelineUsageType pipeline_usage_type)
{
    VK_Context* vk_ctx = VK_CtxGet();
    VK_AssetManager* asset_manager = vk_ctx->asset_manager;
    // ~mgj: Create sampler
    VkSamplerCreateInfo sampler_create_info = {};
    VK_SamplerCreateInfoFromSamplerInfo(sampler_info, &sampler_create_info);
    sampler_create_info.anisotropyEnable = VK_TRUE;
    sampler_create_info.maxAnisotropy = (F32)vk_ctx->msaa_samples;
    VkSampler vk_sampler = VK_SamplerCreate(vk_ctx->device, &sampler_create_info);

    // ~mgj : Choose Descriptor Set Layout
    VkDescriptorSetLayout desc_set_layout = NULL;
    switch (pipeline_usage_type)
    {
        case R_PipelineUsageType_3D:
            desc_set_layout = vk_ctx->model_3D_pipeline.descriptor_set_layout;
            break;
        case R_PipelineUsageType_3DInstanced:
            desc_set_layout = vk_ctx->model_3D_instance_pipeline.descriptor_set_layout;
            break;
        default: InvalidPath;
    }

    // ~mgj: Assign values to texture
    r_AssetItem<VK_Texture>* asset_item =
        VK_AssetManagerItemCreate(&asset_manager->texture_list, &asset_manager->texture_free_list);
    VK_Texture* texture = &asset_item->item;
    texture->desc_set_layout = desc_set_layout;
    texture->sampler = vk_sampler;
    r_Handle texture_handle = {.u64 = (U64)asset_item};

    return texture_handle;
}

static r_Handle
r_texture_load_async(r_SamplerInfo* sampler_info, String8 texture_path,
                     r_PipelineUsageType pipeline_usage_type)
{
    ScratchScope scratch = ScratchScope(0, 0);
    VK_Context* vk_ctx = VK_CtxGet();
    VK_AssetManager* asset_manager = vk_ctx->asset_manager;
    r_ThreadInput* thread_input = VK_ThreadInputCreate();

    // ~mgj: make input ready for texture loading on thread
    r_AssetLoadingInfo* asset_load_info = &thread_input->asset_info;
    asset_load_info->type = R_AssetItemType_Texture;
    r_TextureLoadingInfo* texture_load_info = &asset_load_info->extra_info.texture_info;
    texture_load_info->tex_path = push_str8_copy(thread_input->arena, texture_path);

    asset_load_info->handle = r_texture_handle_create(sampler_info, pipeline_usage_type);

    async::QueueItem queue_input = {.data = thread_input, .worker_func = VK_ThreadSetup};
    async::QueuePush(asset_manager->work_queue, &queue_input);

    return asset_load_info->handle;
}

g_internal void
r_texture_destroy(r_Handle handle)
{
    r_texture_destroy_deferred(handle);
}

g_internal void
r_buffer_destroy(r_Handle handle)
{
    r_buffer_destroy_deferred(handle);
}

g_internal void
r_texture_destroy_deferred(r_Handle handle)
{
    if (r_is_handle_zero(handle) == false)
    {
        VK_Context* vk_ctx = VK_CtxGet();
        vk_deletion_queue_push(vk_ctx->asset_manager->deletion_queue, handle,
                               R_AssetItemType_Texture, VK_MAX_FRAMES_IN_FLIGHT);
    }
}

g_internal void
r_buffer_destroy_deferred(r_Handle handle)
{
    if (r_is_handle_zero(handle) == false)
    {
        VK_Context* vk_ctx = VK_CtxGet();
        vk_deletion_queue_push(vk_ctx->asset_manager->deletion_queue, handle,
                               R_AssetItemType_Buffer, VK_MAX_FRAMES_IN_FLIGHT);
    }
}

static r_Handle
r_buffer_load(r_BufferInfo* buffer_info)
{
    if (buffer_info->buffer.size == 0)
    {
        DEBUG_LOG("Zero handle created for buffer\n");
        return r_handle_zero();
    }

    VK_Context* vk_ctx = VK_CtxGet();
    VK_AssetManager* asset_manager = vk_ctx->asset_manager;
    r_AssetItem<VK_Buffer>* asset_item =
        VK_AssetManagerItemCreate(&asset_manager->buffer_list, &asset_manager->buffer_free_list);

    // ~mgj: Create buffer allocation
    VmaAllocationCreateInfo vma_info = {0};
    vma_info.usage = VMA_MEMORY_USAGE_AUTO;
    vma_info.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    VkBufferUsageFlags usage_flags = {};
    switch (buffer_info->buffer_type)
    {
        case R_BufferType_Vertex: usage_flags = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT; break;
        case R_BufferType_Index: usage_flags = VK_BUFFER_USAGE_INDEX_BUFFER_BIT; break;
        default: InvalidPath;
    }
    VK_BufferAllocation buffer =
        VK_BufferAllocationCreate(vk_ctx->allocator, buffer_info->buffer.size,
                                  usage_flags | VK_BUFFER_USAGE_TRANSFER_DST_BIT, vma_info);

    // ~mgj: Prepare Texture
    VK_Buffer* asset_buffer = &asset_item->item;
    asset_buffer->buffer_alloc = buffer;
    r_Handle buffer_handle = {.u64 = (U64)asset_item};

    // ~mgj: Preparing buffer loading for another thread
    r_ThreadInput* thread_input = VK_ThreadInputCreate();
    r_AssetLoadingInfo* asset_load_info = &thread_input->asset_info;
    asset_load_info->handle = buffer_handle;
    asset_load_info->type = R_AssetItemType_Buffer;
    r_BufferInfo* buffer_load_info = &asset_load_info->extra_info.buffer_info;
    *buffer_load_info = *buffer_info;

    async::QueueItem queue_input = {.data = thread_input, .worker_func = VK_ThreadSetup};
    async::QueuePush(asset_manager->work_queue, &queue_input);

    r_Handle handle = {.u64 = (U64)asset_item};
    return handle;
}

g_internal void
r_texture_gpu_upload_sync(r_Handle tex_handle, Buffer<U8> tex_buf)
{
    VK_Context* vk_ctx = VK_CtxGet();
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
    vk_texture_gpu_upload_cmd_recording(cmd_buf, tex_handle, tex_buf);

    result = vkEndCommandBuffer(cmd_buf);

    VkCommandBufferSubmitInfo cmd_buf_info{};
    cmd_buf_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmd_buf_info.commandBuffer = cmd_buf;

    VkSubmitInfo2 submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submit_info.commandBufferInfoCount = 1;
    submit_info.pCommandBufferInfos = &cmd_buf_info;

    r_AssetItem<VK_Texture>* asset = (r_AssetItem<VK_Texture>*)tex_handle.ptr;
    asset->is_loaded = true;
    result = vkQueueSubmit2(vk_ctx->graphics_queue, 1, &submit_info, NULL);
}

g_internal void
r_model_3D_instance_draw(r_Handle texture_handle, r_Handle vertex_buffer_handle,
                         r_Handle index_buffer_handle, r_BufferInfo* instance_buffer)
{
    VK_Context* vk_ctx = VK_CtxGet();
    VK_AssetManager* asset_manager = vk_ctx->asset_manager;
    r_AssetItem<VK_Buffer>* asset_vertex_buffer =
        (r_AssetItem<VK_Buffer>*)(vertex_buffer_handle.ptr);
    r_AssetItem<VK_Buffer>* asset_index_buffer = (r_AssetItem<VK_Buffer>*)(index_buffer_handle.ptr);
    r_AssetItem<VK_Texture>* asset_texture = (r_AssetItem<VK_Texture>*)(texture_handle.ptr);

    if (asset_vertex_buffer->is_loaded && asset_index_buffer->is_loaded && asset_texture->is_loaded)
    {
        VK_Model3DInstanceBucketAdd(&asset_vertex_buffer->item.buffer_alloc,
                                    &asset_index_buffer->item.buffer_alloc,
                                    asset_texture->item.desc_set, instance_buffer);
    }
}

g_internal void
r_model_3d_draw(r_Model3DPipelineData pipeline_input, B32 depth_test_per_draw_call_only)
{
    VK_Context* vk_ctx = VK_CtxGet();
    VK_AssetManager* asset_manager = vk_ctx->asset_manager;

    if (r_is_handle_zero(pipeline_input.index_buffer_handle) ||
        r_is_handle_zero(pipeline_input.vertex_buffer_handle) ||
        r_is_handle_zero(pipeline_input.texture_handle))
        return;
    r_AssetItem<VK_Buffer>* asset_vertex_buffer =
        (r_AssetItem<VK_Buffer>*)(pipeline_input.vertex_buffer_handle.ptr);
    r_AssetItem<VK_Buffer>* asset_index_buffer =
        (r_AssetItem<VK_Buffer>*)(pipeline_input.index_buffer_handle.ptr);
    r_AssetItem<VK_Texture>* asset_texture =
        (r_AssetItem<VK_Texture>*)(pipeline_input.texture_handle.ptr);

    if (asset_vertex_buffer->is_loaded && asset_index_buffer->is_loaded && asset_texture->is_loaded)
    {
        VK_Model3DBucketAdd(&asset_vertex_buffer->item.buffer_alloc,
                            &asset_index_buffer->item.buffer_alloc, asset_texture->item.desc_set,
                            depth_test_per_draw_call_only, pipeline_input.index_offset,
                            pipeline_input.index_count);
    }
}

g_internal bool
r_is_resource_loaded(r_Handle handle)
{
    // TODO: Refactor asset item to not be a template
    r_AssetItem<S64>* asset = (r_AssetItem<S64>*)handle.ptr;
    return asset->is_loaded;
}

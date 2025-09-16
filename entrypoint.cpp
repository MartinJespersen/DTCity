
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

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = 0;                  // Optional
    beginInfo.pInheritanceInfo = nullptr; // Optional

    wrapper::SwapchainResources* swapchain_resource = vk_ctx->swapchain_resources;
    VkImage swapchain_image = swapchain_resource->image_resources.data[image_index].image;
    // ~mgj: Render scope (Tracy Profiler documentation says this is necessary)
    {
        VkCommandBuffer current_cmd_buf = vk_ctx->command_buffers.data[current_frame];
        VkResult result_vk = vkBeginCommandBuffer(current_cmd_buf, &beginInfo);
        if (result_vk)
        {
            exitWithError("failed to begin recording command buffer!");
        }
        VkImageMemoryBarrier2 transition_to_drawing_barrier{};
        transition_to_drawing_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        transition_to_drawing_barrier.pNext = nullptr;
        transition_to_drawing_barrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
        transition_to_drawing_barrier.srcAccessMask = VK_ACCESS_2_NONE;
        transition_to_drawing_barrier.dstStageMask =
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        transition_to_drawing_barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        transition_to_drawing_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        transition_to_drawing_barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        transition_to_drawing_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        transition_to_drawing_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        transition_to_drawing_barrier.image = swapchain_image;
        transition_to_drawing_barrier.subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                                          .baseMipLevel = 0,
                                                          .levelCount = 1,
                                                          .baseArrayLayer = 0,
                                                          .layerCount = 1};

        VkDependencyInfo to_draw_transition_info = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                                    .imageMemoryBarrierCount = 1,
                                                    .pImageMemoryBarriers =
                                                        &transition_to_drawing_barrier};

        vkCmdPipelineBarrier2(current_cmd_buf, &to_draw_transition_info);
        // ~mgj: this scope is necessary to avoid the vulkan validation error:
        // validation layer: vkDestroyQueryPool(): can't be called on VkQueryPool 0x9638f80000000036
        // that is currently in use by VkCommandBuffer 0x121e6955ed50.
        {
            TracyVkZone(vk_ctx->tracy_ctx[current_frame], current_cmd_buf, "Render");

            VkExtent2D swapchain_extent = swapchain_resource->swapchain_extent;
            VkImageView color_image_view =
                swapchain_resource->color_image_resource.image_view_resource.image_view;
            VkImageView depth_image_view =
                swapchain_resource->depth_image_resource.image_view_resource.image_view;
            VkImageView swapchain_image_view = swapchain_resource->image_resources.data[image_index]
                                                   .image_view_resource.image_view;

            VkClearColorValue clear_color = {0.0f, 0.0f, 0.0f, 1.0f};
            VkClearDepthStencilValue clear_depth = {1.0f, 0};
            VkImageSubresourceRange image_range = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                                   .baseMipLevel = 0,
                                                   .levelCount = 1,
                                                   .baseArrayLayer = 0,
                                                   .layerCount = 1};

            // Color attachment (assuming you want to render to the same targets)
            VkRenderingAttachmentInfo color_attachment{};
            color_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            color_attachment.imageView = color_image_view;
            color_attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            color_attachment.clearValue = {.color = clear_color};

            // Depth attachment
            VkRenderingAttachmentInfo depth_attachment{};
            depth_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            depth_attachment.imageView = depth_image_view;
            depth_attachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            depth_attachment.clearValue = {.depthStencil = clear_depth};

            // Set up MSAA resolve if needed
            if (vk_ctx->msaa_samples != VK_SAMPLE_COUNT_1_BIT)
            {
                color_attachment.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
                color_attachment.resolveImageView = swapchain_image_view;
                color_attachment.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            }

            // Rendering info
            VkRenderingInfo rendering_info{};
            rendering_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            rendering_info.renderArea.offset = {0, 0};
            rendering_info.renderArea.extent = swapchain_extent;
            rendering_info.layerCount = 1;
            rendering_info.colorAttachmentCount = 1;
            rendering_info.pColorAttachments = &color_attachment;
            rendering_info.pDepthAttachment = &depth_attachment;

            wrapper::CameraUniformBufferUpdate(
                vk_ctx, camera,
                Vec2F32{(F32)vk_ctx->swapchain_resources->swapchain_extent.width,
                        (F32)vk_ctx->swapchain_resources->swapchain_extent.height},
                current_frame);

            vkCmdBeginRendering(current_cmd_buf, &rendering_info);

            wrapper::Model3DInstanceRendering();
            wrapper::Model3DRendering();

            IO_InputReset(ctx->io);

            vkCmdEndRendering(current_cmd_buf);

            VkImageMemoryBarrier2 present_barrier{};
            present_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            present_barrier.pNext = nullptr;
            present_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            present_barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            present_barrier.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
            present_barrier.dstAccessMask = VK_ACCESS_2_NONE;
            present_barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            present_barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            present_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            present_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            present_barrier.image = swapchain_image;
            present_barrier.subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                                .baseMipLevel = 0,
                                                .levelCount = 1,
                                                .baseArrayLayer = 0,
                                                .layerCount = 1};

            VkDependencyInfo layout_transition_info = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                                       .imageMemoryBarrierCount = 1,
                                                       .pImageMemoryBarriers = &present_barrier};

            vkCmdPipelineBarrier2(current_cmd_buf, &layout_transition_info);
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

static OS_Handle
Entrypoint(Context* ctx)
{
    ctx->running = 1;
    return OS_ThreadLaunch(MainLoop, ctx, NULL);
}

static void
Cleanup(OS_Handle thread_handle, Context* ctx)
{
    ctx->running = false;
    OS_ThreadJoin(thread_handle, max_U64);
}

static void
MainLoop(void* ptr)
{
    Context* ctx = (Context*)ptr;
    IO* io_ctx = ctx->io;
    OS_SetThreadName(Str8CString("Entrypoint thread"));

    city::GCSBoundingBox gcs_bbox = {.lat_btm_left = 56.16923976826141,
                                     .lon_btm_left = 10.1852768812041,
                                     .lat_top_right = 56.17371342689877,
                                     .lon_top_right = 10.198376789774187};

    ctx->vk_ctx = wrapper::VK_VulkanInit(ctx);
    wrapper::VulkanContext* vk_ctx = wrapper::VulkanCtxGet();

    ctx->road = city::RoadCreate(vk_ctx->texture_path, ctx->cache_path, &gcs_bbox);
    city::Road* road = ctx->road;
    ctx->buildings = city::BuildingsCreate(ctx->cache_path, ctx->vk_ctx->texture_path,
                                           ctx->road->road_height, &gcs_bbox);

    CameraInit(ctx->camera);
    ctx->car_sim = city::CarSimCreate(vk_ctx->asset_path, vk_ctx->texture_path, 100, ctx->road);
    city::CarSim* car_sim = ctx->car_sim;

    render::SamplerInfo sampler_info = {
        .min_filter = render::Filter_Linear,
        .mag_filter = render::Filter_Linear,
        .mip_map_mode = render::MipMapMode_Linear,
        .address_mode_u = render::SamplerAddressMode_Repeat,
        .address_mode_v = render::SamplerAddressMode_Repeat,
    };

    city::Buildings* buildings = ctx->buildings;

    // buildings buffer create
    render::BufferInfo building_vertex_buffer_info =
        render::BufferInfoFromTemplateBuffer(buildings->vertex_buffer);
    render::BufferInfo building_index_buffer_info =
        render::BufferInfoFromTemplateBuffer(buildings->index_buffer);

    // road buffers create
    render::BufferInfo road_vertex_buffer_info =
        render::BufferInfoFromTemplateBuffer(road->vertex_buffer);
    render::BufferInfo road_index_buffer_info =
        render::BufferInfoFromTemplateBuffer(road->index_buffer);

    // car buffers create
    render::BufferInfo car_vertex_buffer_info =
        render::BufferInfoFromTemplateBuffer(car_sim->vertex_buffer);
    render::BufferInfo car_index_buffer_info =
        render::BufferInfoFromTemplateBuffer(car_sim->index_buffer);

    ProfileBuffersCreate(vk_ctx);
    while (ctx->running)
    {
        wrapper::DrawFrameReset();
        UpdateTime(ctx->time);
        CameraUpdate(ctx->camera, ctx->io, ctx->time,
                     vk_ctx->swapchain_resources->swapchain_extent);

        Buffer<city::Model3DInstance> instance_buffer = city::CarUpdate(
            vk_ctx->draw_frame_arena, car_sim, ctx->road, ctx->time->delta_time_sec);
        render::BufferInfo car_instance_buffer_info =
            render::BufferInfoFromTemplateBuffer(instance_buffer);
        wrapper::Model3DDraw(&road->asset_vertex_info, &road->asset_index_info,
                             &road->asset_texture_info, road->texture_path, &sampler_info,
                             &road_vertex_buffer_info, &road_index_buffer_info, TRUE, 0,
                             road->index_buffer.size);

        wrapper::Model3DDraw(&buildings->vertex_buffer_info, &buildings->index_buffer_info,
                             &buildings->roof_texture_info, buildings->roof_texture_path,
                             &sampler_info, &building_vertex_buffer_info,
                             &building_index_buffer_info, FALSE,
                             buildings->roof_index_buffer_offset, buildings->roof_index_count);
        wrapper::Model3DDraw(&buildings->vertex_buffer_info, &buildings->index_buffer_info,
                             &buildings->facade_texture_info, buildings->facade_texture_path,
                             &sampler_info, &building_vertex_buffer_info,
                             &building_index_buffer_info, FALSE,
                             buildings->facade_index_buffer_offset, buildings->facade_index_count);
        wrapper::Model3DInstanceDraw(&car_sim->vertex_buffer_info, &car_sim->index_buffer_info,
                                     &car_sim->texture_info, car_sim->texture_path, &sampler_info,
                                     &car_vertex_buffer_info, &car_index_buffer_info,
                                     &car_instance_buffer_info);

        wrapper::AssetManagerExecuteCmds();
        wrapper::AssetManagerCmdDoneCheck();
        VkSemaphore image_available_semaphore =
            vk_ctx->image_available_semaphores.data[vk_ctx->current_frame];
        VkSemaphore render_finished_semaphore =
            vk_ctx->render_finished_semaphores.data[vk_ctx->current_frame];
        VkFence* in_flight_fence = &vk_ctx->in_flight_fences.data[vk_ctx->current_frame];
        {
            ProfScopeMarkerNamed("Wait for frame");
            VK_CHECK_RESULT(
                vkWaitForFences(vk_ctx->device, 1, in_flight_fence, VK_TRUE, 1000000000));
        }

        uint32_t imageIndex;
        VkResult result = vkAcquireNextImageKHR(
            vk_ctx->device, vk_ctx->swapchain_resources->swapchain, UINT64_MAX,
            image_available_semaphore, VK_NULL_HANDLE, &imageIndex);

        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR ||
            io_ctx->framebuffer_resized)
        {
            io_ctx->framebuffer_resized = false;
            VK_RecreateSwapChain(io_ctx, vk_ctx);
            vk_ctx->current_frame = (vk_ctx->current_frame + 1) % wrapper::MAX_FRAMES_IN_FLIGHT;
            continue;
        }
        else if (result != VK_SUCCESS)
        {
            exitWithError("failed to acquire swap chain image!");
        }

        VkCommandBuffer cmd_buffer = vk_ctx->command_buffers.data[vk_ctx->current_frame];
        VK_CHECK_RESULT(vkResetFences(vk_ctx->device, 1, in_flight_fence));
        VK_CHECK_RESULT(vkResetCommandBuffer(cmd_buffer, 0));

        CommandBufferRecord(imageIndex, vk_ctx->current_frame);

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
        presentInfo.pImageIndices = &imageIndex;
        presentInfo.pResults = nullptr; // Optional

        result = vkQueuePresentKHR(vk_ctx->present_queue, &presentInfo);
        ProfFrameMarker; // end of frame is assumed to be here
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR ||
            io_ctx->framebuffer_resized)
        {
            io_ctx->framebuffer_resized = 0;
            VK_RecreateSwapChain(io_ctx, vk_ctx);
        }
        else if (result != VK_SUCCESS)
        {
            exitWithError("failed to present swap chain image!");
        }

        vk_ctx->current_frame = (vk_ctx->current_frame + 1) % wrapper::MAX_FRAMES_IN_FLIGHT;
    }
    ProfileBuffersDestroy(ctx->vk_ctx);
    vkDeviceWaitIdle(ctx->vk_ctx->device);
    city::RoadDestroy(ctx->road);
    city::CarSimDestroy(ctx->car_sim);
    city::BuildingDestroy(ctx->buildings);
    wrapper::VK_Cleanup(ctx->vk_ctx);
}

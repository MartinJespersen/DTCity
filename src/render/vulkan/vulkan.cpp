namespace vulkan
{

// ~mgj: global context
static Context* g_vk_ctx = 0;

// ~mgj: vulkan context

static void
ctx_set(Context* vk_ctx)
{
    Assert(!g_vk_ctx);
    g_vk_ctx = vk_ctx;
}

static void
ctx_release()
{
    g_vk_ctx = 0;
}

g_internal Context*
ctx_get()
{
    Assert(g_vk_ctx);
    return g_vk_ctx;
}

static void
camera_descriptor_set_layout_create(Context* vk_ctx)
{
    VkDescriptorSetLayoutBinding bindings[] = {
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, NULL},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, NULL},
    };

    VkDescriptorSetLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
    layout_info.bindingCount = ArrayCount(bindings);
    layout_info.pBindings = bindings;

    VK_CHECK_RESULT(
        vkCreateDescriptorSetLayout(vk_ctx->device, &layout_info, nullptr, &vk_ctx->camera_descriptor_set_layout));
}

// ~mgj: Rendering

static void
swapchain_image_barrier_between_rendering(VkCommandBuffer cmd_buffer, VkImage swapchain_image)
{
    VkImageMemoryBarrier2 imgui_render_barrier = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                                                  .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                                                  .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                                                  .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                                                  .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT |
                                                                   VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                                                  .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                                  .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                                  .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                                  .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                                  .image = swapchain_image,
                                                  .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                                                       .baseMipLevel = 0,
                                                                       .levelCount = 1,
                                                                       .baseArrayLayer = 0,
                                                                       .layerCount = 1}};
    VkDependencyInfo renderpass_dep_info = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                            .imageMemoryBarrierCount = 1,
                                            .pImageMemoryBarriers = &imgui_render_barrier};
    vkCmdPipelineBarrier2(cmd_buffer, &renderpass_dep_info);
}

static void
command_buffer_record(U32 image_index, U32 current_frame, Vec2S64 mouse_cursor_pos,
                      render::MappedHandle<void> camera_handle_void)
{
    prof_scope_marker;

    Temp scratch = ScratchBegin(0, 0);

    Context* vk_ctx = ctx_get();

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = 0;
    beginInfo.pInheritanceInfo = nullptr;

    SwapchainResources* swapchain_resource = vk_ctx->swapchain_resources;

    // object id color attachment image
    ImageResource* object_id_image_resource = &swapchain_resource->object_id_image_resources.data[image_index];
    ImageAllocation* object_id_image_alloc = &object_id_image_resource->image_alloc;
    VkImageView object_id_image_view = object_id_image_resource->image_view_resource.image_view;
    VkImage object_id_image = object_id_image_alloc->image;

    // object id resolve image
    ImageResource* object_id_image_resolve_resource =
        &swapchain_resource->object_id_image_resolve_resources.data[image_index];
    VkImageView object_id_image_resolve_view = object_id_image_resolve_resource->image_view_resource.image_view;
    VkImage object_id_resolve_image = object_id_image_resolve_resource->image_alloc.image;

    VkImageView swapchain_image_view =
        swapchain_resource->image_resources.data[image_index].image_view_resource.image_view;

    VkImage swapchain_image = swapchain_resource->image_resources.data[image_index].image;
    // ~mgj: Render scope (Tracy Profiler documentation says this is necessary)
    {
        VkCommandBuffer current_cmd_buf = vk_ctx->command_buffers.data[current_frame];
        VkResult result_vk = vkBeginCommandBuffer(current_cmd_buf, &beginInfo);
        if (result_vk)
        {
            exit_with_error("failed to begin recording command buffer!");
        }
        VkImageMemoryBarrier2 pre_render_swapchain_barrier{};
        pre_render_swapchain_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        pre_render_swapchain_barrier.pNext = nullptr;
        pre_render_swapchain_barrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
        pre_render_swapchain_barrier.srcAccessMask = VK_ACCESS_2_NONE;
        pre_render_swapchain_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        pre_render_swapchain_barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        pre_render_swapchain_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        pre_render_swapchain_barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        pre_render_swapchain_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        pre_render_swapchain_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        pre_render_swapchain_barrier.image = swapchain_image;
        pre_render_swapchain_barrier.subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                                         .baseMipLevel = 0,
                                                         .levelCount = 1,
                                                         .baseArrayLayer = 0,
                                                         .layerCount = 1};

        B32 shared_attachments_initialized = swapchain_resource->shared_attachment_images_initialized;
        VkImageMemoryBarrier2 pre_render_color_barrier{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = shared_attachments_initialized ? VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT
                                                           : VK_PIPELINE_STAGE_2_NONE,
            .srcAccessMask = shared_attachments_initialized ? VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT : VK_ACCESS_2_NONE,
            .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            .oldLayout =
                shared_attachments_initialized ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = swapchain_resource->color_image_resource.image_alloc.image,
            .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                 .baseMipLevel = 0,
                                 .levelCount = 1,
                                 .baseArrayLayer = 0,
                                 .layerCount = 1},
        };

        VkPipelineStageFlags2 depth_stages =
            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        VkImageMemoryBarrier2 pre_render_depth_barrier{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = shared_attachments_initialized ? depth_stages : VK_PIPELINE_STAGE_2_NONE,
            .srcAccessMask =
                shared_attachments_initialized ? VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT : VK_ACCESS_2_NONE,
            .dstStageMask = depth_stages,
            .dstAccessMask =
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .oldLayout = shared_attachments_initialized ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                                                        : VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = swapchain_resource->depth_image_resource.image_alloc.image,
            .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                                 .baseMipLevel = 0,
                                 .levelCount = 1,
                                 .baseArrayLayer = 0,
                                 .layerCount = 1},
        };

        B32 object_id_images_initialized = swapchain_resource->object_id_images_initialized.data[image_index];

        VkImageMemoryBarrier2 pre_render_object_id_barrier{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = object_id_images_initialized ? VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT
                                                         : VK_PIPELINE_STAGE_2_NONE,
            .srcAccessMask = object_id_images_initialized ? VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT : VK_ACCESS_2_NONE,
            .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            .oldLayout =
                object_id_images_initialized ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = object_id_image,
            .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                 .baseMipLevel = 0,
                                 .levelCount = 1,
                                 .baseArrayLayer = 0,
                                 .layerCount = 1}};

        VkImageMemoryBarrier2 pre_render_object_id_resolve_image_barrier{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = object_id_images_initialized
                                ? VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_2_TRANSFER_BIT
                                : VK_PIPELINE_STAGE_2_NONE,
            .srcAccessMask = object_id_images_initialized
                                 ? VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_TRANSFER_READ_BIT
                                 : VK_ACCESS_2_NONE,
            .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            .oldLayout =
                object_id_images_initialized ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = object_id_resolve_image,
            .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                 .baseMipLevel = 0,
                                 .levelCount = 1,
                                 .baseArrayLayer = 0,
                                 .layerCount = 1}};

        VkImageMemoryBarrier2 pre_render_barriers[] = {pre_render_color_barrier, pre_render_depth_barrier,
                                                       pre_render_object_id_barrier, pre_render_swapchain_barrier,
                                                       pre_render_object_id_resolve_image_barrier};
        VkDependencyInfo pre_render_transition_info = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                                       .imageMemoryBarrierCount = ArrayCount(pre_render_barriers),
                                                       .pImageMemoryBarriers = pre_render_barriers};

        vkCmdPipelineBarrier2(current_cmd_buf, &pre_render_transition_info);
        swapchain_resource->shared_attachment_images_initialized = true;
        swapchain_resource->object_id_images_initialized.data[image_index] = true;
        {
            TracyVkZone(vk_ctx->tracy_ctx[current_frame], current_cmd_buf, "Render"); // NOLINT

            VkExtent2D swapchain_extent = swapchain_resource->swapchain_extent;
            VkImageView color_image_view = swapchain_resource->color_image_resource.image_view_resource.image_view;
            VkImageView depth_image_view = swapchain_resource->depth_image_resource.image_view_resource.image_view;

            VkClearColorValue clear_color = {84.0f / 255.999f, 84.0f / 255.999f, 84.0f / 255.999f, 1.0f};
            VkClearDepthStencilValue clear_depth = {1.0f, 0};

            VkRenderingAttachmentInfo color_attachment{};
            color_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            color_attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            color_attachment.clearValue = {.color = clear_color};

            VkRenderingAttachmentInfo object_id_attachment{};
            object_id_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            object_id_attachment.imageView = object_id_image_view;
            object_id_attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            object_id_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            object_id_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            object_id_attachment.clearValue = {.color = clear_color};

            if (vk_ctx->msaa_samples == VK_SAMPLE_COUNT_1_BIT)
            {
                color_attachment.imageView = swapchain_image_view;
                object_id_attachment.imageView = object_id_image_resolve_view;
            }
            else
            {
                color_attachment.imageView = color_image_view;
                color_attachment.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
                color_attachment.resolveImageView = swapchain_image_view;
                color_attachment.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

                object_id_attachment.imageView = object_id_image_view;
                object_id_attachment.resolveMode = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT;
                object_id_attachment.resolveImageView = object_id_image_resolve_view;
                object_id_attachment.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            }

            VkRenderingAttachmentInfo depth_attachment{};
            depth_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            depth_attachment.imageView = depth_image_view;
            depth_attachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            depth_attachment.clearValue = {.depthStencil = clear_depth};

            VkRenderingAttachmentInfo color_attachments[] = {color_attachment, object_id_attachment};
            VkRenderingInfo rendering_info{};
            rendering_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            rendering_info.renderArea.offset = {0, 0};
            rendering_info.renderArea.extent = swapchain_extent;
            rendering_info.layerCount = 1;
            rendering_info.colorAttachmentCount = ArrayCount(color_attachments);
            rendering_info.pColorAttachments = color_attachments;
            rendering_info.pDepthAttachment = &depth_attachment;

            VkDebugUtilsLabelEXT debug_label{};
            debug_label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;

            // ~mgj: Render pass
            vkCmdBeginRendering(current_cmd_buf, &rendering_info);

            debug_label.pLabelName = "Model 3D Rendering";
            CMD_BEGIN_DEBUG_UTILS_LABEL_EXT(current_cmd_buf, &debug_label);
            tile_rendering(camera_handle_void);
            CMD_END_DEBUG_UTILS_LABEL_EXT(current_cmd_buf);

            blend_3d_rendering();

            draw::DrawFrame* draw_frame = draw::draw_frame_get();
            Buffer<render::PrimitiveInstance> primitive_instances = {};
            if (draw_frame->primitive_instance_list.count > 0)
            {
                primitive_instances = buffer_alloc<render::PrimitiveInstance>(
                    vk_ctx->render_frame_arena, draw_frame->primitive_instance_list.count);
                U64 primitive_instance_idx = 0;
                for (draw::PrimitiveInstanceNode* node = draw_frame->primitive_instance_list.first; node;
                     node = node->next)
                {
                    render::PrimitiveInstance* primitive_instance = &primitive_instances.data[primitive_instance_idx++];
                    primitive_instance->locations =
                        buffer_from_chunk_list(vk_ctx->render_frame_arena, node->location_chunk_list);
                    primitive_instance->scale_factor = node->scale_factor;
                    primitive_instance->mesh_handle = node->mesh_handle;
                }
            }

            primitive_rendering(primitive_instances, camera_handle_void);

            if (draw_frame->mesh_instance_batches->total_count > 0)
            {
                Buffer<render::MeshInstanceBatch> mesh_batches =
                    buffer_from_chunk_list(vk_ctx->render_frame_arena, draw_frame->mesh_instance_batches);
                mesh_instance_rendering(mesh_batches, camera_handle_void);
            }

            if (draw_frame->line_vertex_chunk_list->total_count > 0)
            {
                Buffer<render::LineVertex> line_vertices =
                    buffer_from_chunk_list(vk_ctx->render_frame_arena, draw_frame->line_vertex_chunk_list);
                lines_render(line_vertices, camera_handle_void);
            }

            vkCmdEndRendering(current_cmd_buf);

            swapchain_image_barrier_between_rendering(current_cmd_buf, swapchain_image);

            VkRenderingAttachmentInfo imgui_color_attachment{};
            imgui_color_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            imgui_color_attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            imgui_color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            imgui_color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            imgui_color_attachment.clearValue = {.color = clear_color};
            imgui_color_attachment.imageView = swapchain_image_view;

            VkRenderingInfo imgui_rendering_info{};
            imgui_rendering_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            imgui_rendering_info.renderArea.offset = {0, 0};
            imgui_rendering_info.renderArea.extent = swapchain_extent;
            imgui_rendering_info.layerCount = 1;
            imgui_rendering_info.colorAttachmentCount = 1;
            imgui_rendering_info.pColorAttachments = &imgui_color_attachment;

            vkCmdBeginRendering(current_cmd_buf, &imgui_rendering_info);

            ImGui::Render();
            ImDrawData* draw_data = ImGui::GetDrawData();
            const bool is_minimized = (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f);
            if (!is_minimized)
            {
                ImGui_ImplVulkan_RenderDrawData(draw_data, current_cmd_buf);
            }

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
            VkImageMemoryBarrier2 object_id_read_barrier = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT,
                .dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = object_id_resolve_image,
                .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                     .baseMipLevel = 0,
                                     .levelCount = 1,
                                     .baseArrayLayer = 0,
                                     .layerCount = 1}};

            VkImageMemoryBarrier2 post_render_barriers[] = {present_barrier, object_id_read_barrier};
            VkDependencyInfo layout_transition_info = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                                       .imageMemoryBarrierCount = ArrayCount(post_render_barriers),
                                                       .pImageMemoryBarriers = post_render_barriers};

            vkCmdPipelineBarrier2(current_cmd_buf, &layout_transition_info);

            Vec2S64 mouse_position_screen_coords = mouse_cursor_pos;
            if (mouse_position_screen_coords.x > 0 && mouse_position_screen_coords.y > 0 &&
                mouse_position_screen_coords.x < vk_ctx->swapchain_resources->swapchain_extent.width &&
                mouse_position_screen_coords.y < vk_ctx->swapchain_resources->swapchain_extent.height)
            {
                VkBufferMemoryBarrier2 readback_barrier = {
                    .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                    .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
                    .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                    .dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
                    .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .buffer = swapchain_resource->object_id_buffer_readback[current_frame].buffer_alloc.buffer,
                    .offset = 0,
                    .size = VK_WHOLE_SIZE};
                VkDependencyInfo readback_dep_info = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                                      .bufferMemoryBarrierCount = 1,
                                                      .pBufferMemoryBarriers = &readback_barrier};
                vkCmdPipelineBarrier2(current_cmd_buf, &readback_dep_info);

                VkBufferImageCopy buffer_image_copy[] = {{
                    .imageSubresource =
                        {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1},
                    .imageOffset = {(S32)mouse_position_screen_coords.x, (S32)mouse_position_screen_coords.y, 0},
                    .imageExtent = {1, 1, 1},
                }};
                vkCmdCopyImageToBuffer(current_cmd_buf, object_id_resolve_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                       swapchain_resource->object_id_buffer_readback[current_frame].buffer_alloc.buffer,
                                       ArrayCount(buffer_image_copy), buffer_image_copy);

                Assert(vk_ctx->object_id_format == VK_FORMAT_R32G32_UINT);
                U64* object_id = (U64*)swapchain_resource->object_id_buffer_readback[current_frame].mapped_ptr;
                vk_ctx->hovered_object_id = *object_id;
            }
        }

        TracyVkCollect(vk_ctx->tracy_ctx[current_frame], current_cmd_buf);

        VkResult result = vkEndCommandBuffer(current_cmd_buf);
        if (result)
        {
            exit_with_error("failed to record command buffer!");
        }
    }
    ScratchEnd(scratch);
}

g_internal void
mapped_buffers_update()
{
    prof_scope_marker;

    vulkan::Context* vk_ctx = vulkan::ctx_get();
    for (LinkedListNode<MappedHandleTransfer>* m_handle_transfer = vk_ctx->mapped_handle_list.first; m_handle_transfer;
         m_handle_transfer = m_handle_transfer->next)
    {
        render::MappedHandleFrame<void>* frame_handle =
            &m_handle_transfer->v.mapped_handle.buffer.data[vk_ctx->current_frame];

        render::AssetItem<vulkan::BufferHandle>* mapped_buffer_handle =
            vulkan::asset_manager_buffer_item_get(frame_handle->handle);
        vulkan::BufferHandle* mapped_buffer = &mapped_buffer_handle->item;

        MemoryCopy(frame_handle->data, m_handle_transfer->v.source.str, m_handle_transfer->v.source.size);

        vulkan::AssetManager* asset_manager = vulkan::asset_manager_get();
        U32 mapped_buffer_size = mapped_buffer->buffer_alloc.size;

        AssertAlways(mapped_buffer->mem_prop_flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
        if ((mapped_buffer->mem_prop_flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0)
        {
            VK_CHECK_RESULT(vmaFlushAllocation(asset_manager->allocator, mapped_buffer->buffer_alloc.allocation, 0,
                                               mapped_buffer_size));
        }
    }
    vk_ctx->mapped_handle_list = {};
}

static void
profile_buffers_create(Context* vk_ctx)
{
#ifdef TRACY_ENABLE
    for (U32 i = 0; i < ArrayCount(vk_ctx->tracy_ctx); i++)
    {
        vk_ctx->tracy_ctx[i] = TracyVkContext(vk_ctx->physical_device, vk_ctx->device, vk_ctx->graphics_queue,
                                              vk_ctx->command_buffers.data[i]);
    }
#endif
}

static void
profile_buffers_destroy(Context* vk_ctx)
{
#ifdef TRACY_ENABLE
    vkQueueWaitIdle(vk_ctx->graphics_queue);
    for (U32 i = 0; i < ArrayCount(vk_ctx->tracy_ctx); i++)
    {
        TracyVkDestroy(vk_ctx->tracy_ctx[i]);
    }
#endif
}

} // namespace vulkan

namespace vulkan
{
g_internal void
pipeline_destroy(Pipeline* pipeline)
{
    Context* vk_ctx = ctx_get();
    vkDestroyPipeline(vk_ctx->device, pipeline->pipeline, NULL);
    vkDestroyPipelineLayout(vk_ctx->device, pipeline->pipeline_layout, NULL);
}
g_internal Pipeline
agent_instance_pipeline_create(Context* vk_ctx, String8 shader_path)
{
    ScratchScope scratch = ScratchScope(0, 0);

    String8 vert_path = CreatePathFromStrings(scratch.arena, Str8BufferFromCString(scratch.arena, {(char*)shader_path.str, "bin", "model_3d_instancing_vert.spv"}));
    String8 frag_path = CreatePathFromStrings(scratch.arena, Str8BufferFromCString(scratch.arena, {(char*)shader_path.str, "bin", "model_3d_instancing_frag.spv"}));

    ShaderModuleInfo vert_shader_stage_info = shader_stage_from_spirv(scratch.arena, vk_ctx->device, VK_SHADER_STAGE_VERTEX_BIT, vert_path);
    ShaderModuleInfo frag_shader_stage_info = shader_stage_from_spirv(scratch.arena, vk_ctx->device, VK_SHADER_STAGE_FRAGMENT_BIT, frag_path);

    VkPipelineShaderStageCreateInfo shader_stages[] = {
        vert_shader_stage_info.info,
        frag_shader_stage_info.info,
    };

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

    VkPipelineDynamicStateCreateInfo dynamic_state{};
    dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount = (U32)(ArrayCount(dynamicStates));
    dynamic_state.pDynamicStates = dynamicStates;

    VkPipelineVertexInputStateCreateInfo vertex_input_info{};
    vertex_input_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    U32 uv_offset = (U32)offsetof(render::TileVertex, uv);
    U32 x_basis_offset = (U32)offsetof(render::Transform, x_basis);
    U32 y_basis_offset = (U32)offsetof(render::Transform, y_basis);
    U32 z_basis_offset = (U32)offsetof(render::Transform, z_basis);
    U32 w_basis_offset = (U32)offsetof(render::Transform, w_basis);

    VkVertexInputAttributeDescription attr_desc[] = {
        {.location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT},
        {.location = 1, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = uv_offset},
        {.location = 2, .binding = 1, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = x_basis_offset},
        {.location = 3, .binding = 1, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = y_basis_offset},
        {.location = 4, .binding = 1, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = z_basis_offset},
        {.location = 5, .binding = 1, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = w_basis_offset},
    };
    VkVertexInputBindingDescription input_desc[] = {{.binding = 0, .stride = sizeof(render::TileVertex), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX},
                                                    {.binding = 1, .stride = sizeof(render::Transform), .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE}};

    vertex_input_info.vertexBindingDescriptionCount = ArrayCount(input_desc);
    vertex_input_info.vertexAttributeDescriptionCount = ArrayCount(attr_desc);
    vertex_input_info.pVertexBindingDescriptions = input_desc;
    vertex_input_info.pVertexAttributeDescriptions = attr_desc;

    VkPipelineInputAssemblyStateCreateInfo input_assembly{};
    input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (F32)vk_ctx->swapchain_resources->swapchain_extent.width;
    viewport.height = (F32)vk_ctx->swapchain_resources->swapchain_extent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = vk_ctx->swapchain_resources->swapchain_extent;

    VkPipelineViewportStateCreateInfo viewport_state{};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.scissorCount = 1;
    viewport_state.pViewports = &viewport;
    viewport_state.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_TRUE;
    multisampling.rasterizationSamples = vk_ctx->msaa_samples;
    multisampling.minSampleShading = 1.0f;

    VkPipelineColorBlendAttachmentState color_blend_attachment{.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT};

    VkPipelineColorBlendAttachmentState color_blend_attachments[] = {color_blend_attachment, color_blend_attachment};
    VkPipelineColorBlendStateCreateInfo color_blending{};
    color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blending.attachmentCount = ArrayCount(color_blend_attachments);
    color_blending.pAttachments = color_blend_attachments;

    VkPushConstantRange push_constant_range{};
    push_constant_range.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    push_constant_range.offset = 0;
    push_constant_range.size = sizeof(CarInstancePushConstants);

    VkDescriptorSetLayout descriptor_set_layouts[] = {vk_ctx->camera_descriptor_set_layout, vk_ctx->bindless_descriptor_set_layout};

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = ArrayCount(descriptor_set_layouts);
    pipelineLayoutInfo.pSetLayouts = descriptor_set_layouts;
    pipelineLayoutInfo.pPushConstantRanges = &push_constant_range;
    pipelineLayoutInfo.pushConstantRangeCount = 1;

    VkPipelineDepthStencilStateCreateInfo depth_stencil{};
    depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil.depthTestEnable = VK_TRUE;
    depth_stencil.depthWriteEnable = VK_TRUE;
    depth_stencil.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineLayout pipeline_layout;
    if (vkCreatePipelineLayout(vk_ctx->device, &pipelineLayoutInfo, nullptr, &pipeline_layout) != VK_SUCCESS)
    {
        exit_with_error("failed to create pipeline layout!");
    }

    VkFormat color_attachment_formats[] = {vk_ctx->swapchain_resources->color_format, vk_ctx->swapchain_resources->object_id_image_format};

    VkPipelineRenderingCreateInfo pipeline_rendering_info{};
    pipeline_rendering_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    pipeline_rendering_info.colorAttachmentCount = ArrayCount(color_attachment_formats);
    pipeline_rendering_info.pColorAttachmentFormats = color_attachment_formats;
    pipeline_rendering_info.depthAttachmentFormat = vk_ctx->swapchain_resources->depth_format;

    VkGraphicsPipelineCreateInfo pipeline_create_info{};
    pipeline_create_info.pNext = &pipeline_rendering_info;
    pipeline_create_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_create_info.stageCount = ArrayCount(shader_stages);
    pipeline_create_info.pStages = shader_stages;
    pipeline_create_info.pVertexInputState = &vertex_input_info;
    pipeline_create_info.pInputAssemblyState = &input_assembly;
    pipeline_create_info.pViewportState = &viewport_state;
    pipeline_create_info.pRasterizationState = &rasterizer;
    pipeline_create_info.pMultisampleState = &multisampling;
    pipeline_create_info.pDepthStencilState = &depth_stencil;
    pipeline_create_info.pColorBlendState = &color_blending;
    pipeline_create_info.pDynamicState = &dynamic_state;
    pipeline_create_info.layout = pipeline_layout;

    VkPipeline pipeline;
    if (vkCreateGraphicsPipelines(vk_ctx->device, VK_NULL_HANDLE, 1, &pipeline_create_info, nullptr, &pipeline) != VK_SUCCESS)
    {
        exit_with_error("failed to create graphics pipeline!");
    }

    Pipeline pipeline_info = {.pipeline = pipeline, .pipeline_layout = pipeline_layout};
    return pipeline_info;
}

g_internal void
agent_instance_rendering()
{
    Context* vk_ctx = ctx_get();
    VkCommandBuffer cmd_buffer = vk_ctx->command_buffers.data[vk_ctx->current_frame];
    TracyVkZone(vk_ctx->tracy_ctx[vk_ctx->current_frame], cmd_buffer, "car_instance_rendering");

    SwapchainResources* swapchain_resources = vk_ctx->swapchain_resources;
    VkExtent2D swapchain_extent = swapchain_resources->swapchain_extent;
    Pipeline* pipeline = &vk_ctx->car_instance_pipeline;

    // prepare pipeline
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (F32)swapchain_extent.width;
    viewport.height = (F32)swapchain_extent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd_buffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = swapchain_extent;
    vkCmdSetScissor(cmd_buffer, 0, 1, &scissor);

    vkCmdBindPipeline(cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline);

    render::AssetItem<BufferHandle>* instance_buffer_handle = asset_manager_buffer_item_get(vk_ctx->model_3D_instance_buffer[vk_ctx->current_frame]);
    if (!instance_buffer_handle)
    {
        return;
    }

    BufferAllocation instance_buffer_alloc = instance_buffer_handle->item.buffer_alloc;
    VkBuffer instance_buffer = instance_buffer_alloc.buffer;
    VkDescriptorSet descriptor_sets[1] = {vk_ctx->bindless_descriptor_set};

    for (CarInstanceRenderNode* node = vk_ctx->render_frame->car_instance_render_list.list.first; node; node = node->next)
    {
        VK_CHECK_RESULT(vmaCopyMemoryToAllocation(vk_ctx->asset_manager->allocator, node->instance_buffer_info.buffer.data, instance_buffer_alloc.allocation, node->instance_buffer_offset,
                                                  node->instance_buffer_info.buffer.size));

        render::Handle camera_handle = node->camera_handle.buffer[vk_ctx->current_frame]->handle;
        render::AssetItem<BufferHandle>* asset_item = asset_manager_buffer_item_get(camera_handle);
        BufferHandle* camera_buffer = &asset_item->item;

        VkDescriptorBufferInfo camera_buffer_info{};
        camera_buffer_info.buffer = camera_buffer->buffer_alloc.buffer;
        camera_buffer_info.offset = 0;
        camera_buffer_info.range = VK_WHOLE_SIZE;

        VkWriteDescriptorSet push_writes[] = {
            {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstBinding = 0, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .pBufferInfo = &camera_buffer_info},
        };

        cmd_push_descriptor_set_khr(cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline_layout, 0, ArrayCount(push_writes), push_writes);
        vkCmdBindDescriptorSets(cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline_layout, 1, ArrayCount(descriptor_sets), descriptor_sets, 0, NULL);
        U32 instance_count = U32(node->instance_buffer_info.buffer.size / node->instance_buffer_info.type_size);
        VkDeviceSize vertex_offsets[] = {0, node->instance_buffer_offset};
        for (U32 mesh_idx = 0; mesh_idx < node->meshes.size; ++mesh_idx)
        {
            render::AgentModelInfo* mesh = node->meshes[mesh_idx];
            render::AssetItem<BufferHandle>* vertex_item = asset_manager_buffer_item_get(mesh->vertex_handle);
            render::AssetItem<BufferHandle>* index_item = asset_manager_buffer_item_get(mesh->index_handle);
            if (mesh->texture_handle_idx >= node->texture_handles.size)
            {
                continue;
            }
            render::Handle texture_handle = node->texture_handles.data[mesh->texture_handle_idx];
            render::AssetItem<TextureHandle>* texture_item = asset_manager_texture_item_get(texture_handle);
            if (!vertex_item || !index_item || !texture_item)
            {
                continue;
            }

            BufferHandle* vertex_handle = &vertex_item->item;
            BufferHandle* index_handle = &index_item->item;
            TextureHandle* texture = &texture_item->item;
            CarInstancePushConstants push_constants = {.color = mesh->color, .tex_idx = texture->descriptor_set_idx};
            VkBuffer vertex_buffers[] = {
                vertex_handle->buffer_alloc.buffer,
                instance_buffer,
            };

            vkCmdPushConstants(cmd_buffer, pipeline->pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(CarInstancePushConstants), &push_constants);
            vkCmdBindVertexBuffers(cmd_buffer, 0, 2, vertex_buffers, vertex_offsets);
            vkCmdBindIndexBuffer(cmd_buffer, index_handle->buffer_alloc.buffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cmd_buffer, index_handle->elem_count, instance_count, 0, 0, 0);
        }
    }
}

g_internal void
draw_indexed_separate_depth_and_color_calls(VkCommandBuffer cmd_buffer, U32 index_offset, U32 index_count, VkCompareOp depth_compare_op)
{
    VkBool32 color_write_enabled[4] = {VK_TRUE, VK_TRUE, VK_TRUE, VK_TRUE};
    VkBool32 color_write_disabled[4] = {};
    vkCmdSetDepthCompareOp(cmd_buffer, depth_compare_op);
    for (WriteType write_type = (WriteType)0; write_type < WriteType_Count; write_type = (WriteType)(write_type + 1))
    {
        if (write_type == WriteType_Color)
        {
            vkCmdSetDepthWriteEnable(cmd_buffer, VK_FALSE);
            cmd_set_color_write_enable_ext(cmd_buffer, ArrayCount(color_write_enabled), color_write_enabled);
        }
        else if (write_type == WriteType_Depth)
        {
            vkCmdSetDepthWriteEnable(cmd_buffer, VK_TRUE);
            cmd_set_color_write_enable_ext(cmd_buffer, ArrayCount(color_write_disabled), color_write_disabled);
        }

        vkCmdDrawIndexed(cmd_buffer, index_count, 1, index_offset, 0, 0);
    }
}

g_internal Pipeline
tile_pipeline_create(Context* vk_ctx, String8 shader_path)
{
    ScratchScope scratch = ScratchScope(0, 0);

    String8 vert_path = CreatePathFromStrings(scratch.arena, Str8BufferFromCString(scratch.arena, {(char*)shader_path.str, "bin", "tile_vert.spv"}));
    String8 frag_path = CreatePathFromStrings(scratch.arena, Str8BufferFromCString(scratch.arena, {(char*)shader_path.str, "bin", "tile_frag.spv"}));

    ShaderModuleInfo vert_shader_stage_info = shader_stage_from_spirv(scratch.arena, vk_ctx->device, VK_SHADER_STAGE_VERTEX_BIT, vert_path);
    ShaderModuleInfo frag_shader_stage_info = shader_stage_from_spirv(scratch.arena, vk_ctx->device, VK_SHADER_STAGE_FRAGMENT_BIT, frag_path);

    VkPipelineShaderStageCreateInfo shader_stages[] = {
        vert_shader_stage_info.info,
        frag_shader_stage_info.info,
    };

    VkDynamicState dynamicStates[] = {
        VK_DYNAMIC_STATE_VIEWPORT,  VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE, VK_DYNAMIC_STATE_DEPTH_COMPARE_OP, VK_DYNAMIC_STATE_COLOR_WRITE_ENABLE_EXT,
        VK_DYNAMIC_STATE_DEPTH_BIAS};

    VkPipelineDynamicStateCreateInfo dynamic_state{};
    dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount = (U32)(ArrayCount(dynamicStates));
    dynamic_state.pDynamicStates = dynamicStates;

    VkPipelineVertexInputStateCreateInfo vertex_input_info{};
    vertex_input_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    U32 pos_offset = offsetof(render::TileVertex, pos);
    U32 uv_offset = offsetof(render::TileVertex, uv);
    U32 overlay_uv_offset = offsetof(render::TileVertex, overlay_uv);
    U32 road_segment_idx_offset = offsetof(render::TileVertex, road_segment_idx);

    VkVertexInputAttributeDescription attr_desc[] = {{.location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = pos_offset},
                                                     {.location = 1, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = uv_offset},
                                                     {.location = 2, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = overlay_uv_offset},
                                                     {.location = 3, .binding = 0, .format = VK_FORMAT_R32_UINT, .offset = road_segment_idx_offset}};

    VkVertexInputBindingDescription input_desc[] = {{.binding = 0, .stride = sizeof(render::TileVertex), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX}};

    vertex_input_info.vertexBindingDescriptionCount = ArrayCount(input_desc);
    vertex_input_info.vertexAttributeDescriptionCount = ArrayCount(attr_desc);
    vertex_input_info.pVertexBindingDescriptions = input_desc;
    vertex_input_info.pVertexAttributeDescriptions = attr_desc;

    VkPipelineInputAssemblyStateCreateInfo input_assembly{};
    input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (F32)vk_ctx->swapchain_resources->swapchain_extent.width;
    viewport.height = (F32)vk_ctx->swapchain_resources->swapchain_extent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = vk_ctx->swapchain_resources->swapchain_extent;

    VkPipelineViewportStateCreateInfo viewport_state{};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.scissorCount = 1;
    viewport_state.pViewports = &viewport;
    viewport_state.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_TRUE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_TRUE;
    multisampling.rasterizationSamples = vk_ctx->msaa_samples;
    multisampling.minSampleShading = 1.0f;

    VkPipelineColorBlendAttachmentState color_blend_attachment{.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT};

    VkPipelineColorBlendAttachmentState color_blend_attachments[] = {color_blend_attachment, color_blend_attachment};
    VkPipelineColorBlendStateCreateInfo color_blending{};
    color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blending.attachmentCount = ArrayCount(color_blend_attachments);
    color_blending.pAttachments = color_blend_attachments;

    VkDescriptorSetLayout descriptor_set_layouts[] = {vk_ctx->camera_descriptor_set_layout, vk_ctx->bindless_descriptor_set_layout};

    VkPushConstantRange push_constant_range{};
    push_constant_range.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT;
    push_constant_range.offset = 0;
    push_constant_range.size = sizeof(TilePipelinePushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = ArrayCount(descriptor_set_layouts);
    pipelineLayoutInfo.pSetLayouts = descriptor_set_layouts;
    pipelineLayoutInfo.pPushConstantRanges = &push_constant_range;
    pipelineLayoutInfo.pushConstantRangeCount = 1;

    VkPipelineDepthStencilStateCreateInfo depth_stencil{};
    depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil.depthTestEnable = VK_TRUE;
    depth_stencil.depthWriteEnable = VK_TRUE;
    depth_stencil.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineLayout pipeline_layout;
    if (vkCreatePipelineLayout(vk_ctx->device, &pipelineLayoutInfo, nullptr, &pipeline_layout) != VK_SUCCESS)
    {
        exit_with_error("failed to create pipeline layout!");
    }

    VkFormat color_attachment_formats[] = {vk_ctx->swapchain_resources->color_format, vk_ctx->swapchain_resources->object_id_image_format};
    VkPipelineRenderingCreateInfo pipeline_rendering_info{};
    pipeline_rendering_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    pipeline_rendering_info.colorAttachmentCount = ArrayCount(color_attachment_formats);
    pipeline_rendering_info.pColorAttachmentFormats = color_attachment_formats;
    pipeline_rendering_info.depthAttachmentFormat = vk_ctx->swapchain_resources->depth_format;

    VkGraphicsPipelineCreateInfo pipeline_create_info{};
    pipeline_create_info.pNext = &pipeline_rendering_info;
    pipeline_create_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_create_info.stageCount = ArrayCount(shader_stages);
    pipeline_create_info.pStages = shader_stages;
    pipeline_create_info.pVertexInputState = &vertex_input_info;
    pipeline_create_info.pInputAssemblyState = &input_assembly;
    pipeline_create_info.pViewportState = &viewport_state;
    pipeline_create_info.pRasterizationState = &rasterizer;
    pipeline_create_info.pMultisampleState = &multisampling;
    pipeline_create_info.pDepthStencilState = &depth_stencil;
    pipeline_create_info.pColorBlendState = &color_blending;
    pipeline_create_info.pDynamicState = &dynamic_state;
    pipeline_create_info.layout = pipeline_layout;

    VkPipeline pipeline;
    if (vkCreateGraphicsPipelines(vk_ctx->device, VK_NULL_HANDLE, 1, &pipeline_create_info, nullptr, &pipeline) != VK_SUCCESS)
    {
        exit_with_error("failed to create graphics pipeline!");
    }

    Pipeline pipeline_info = {.pipeline = pipeline, .pipeline_layout = pipeline_layout};
    return pipeline_info;
}

g_internal void
tile_rendering()
{
    Context* vk_ctx = ctx_get();
    VkCommandBuffer cmd_buffer = vk_ctx->command_buffers.data[vk_ctx->current_frame];
    TracyVkZone(vk_ctx->tracy_ctx[vk_ctx->current_frame], cmd_buffer, "tile_rendering");

    Pipeline* model_3D_pipeline = &vk_ctx->model_3D_pipeline;
    RenderFrame* render_frame = vk_ctx->render_frame;

    SwapchainResources* swapchain_resources = vk_ctx->swapchain_resources;
    VkExtent2D swapchain_extent = swapchain_resources->swapchain_extent;

    vkCmdBindPipeline(cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, model_3D_pipeline->pipeline);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (F32)(swapchain_extent.width);
    viewport.height = (F32)(swapchain_extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd_buffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = swapchain_extent;
    vkCmdSetScissor(cmd_buffer, 0, 1, &scissor);

    VkDescriptorSet descriptor_sets[1] = {vk_ctx->bindless_descriptor_set};

    VkDeviceSize offsets[] = {0};
    for (TilePipelineNode* node = render_frame->model_3D_list.first; node; node = node->next)
    {
        render::Handle camera_handle = node->camera_handle.buffer[vk_ctx->current_frame]->handle;
        render::AssetItem<BufferHandle>* camera_buffer_handle = asset_manager_buffer_item_get(camera_handle);
        AssertAlways(camera_buffer_handle);
        BufferHandle* camera_buffer = &camera_buffer_handle->item;

        VkDescriptorBufferInfo camera_buffer_info{};
        camera_buffer_info.buffer = camera_buffer->buffer_alloc.buffer;
        camera_buffer_info.offset = 0;
        camera_buffer_info.range = VK_WHOLE_SIZE;

        VkDescriptorBufferInfo road_segment_buffer_info{};
        road_segment_buffer_info.buffer = node->road_segment_alloc.buffer;
        road_segment_buffer_info.offset = 0;
        road_segment_buffer_info.range = node->road_segment_alloc.size;

        VkWriteDescriptorSet push_writes[] = {
            {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstBinding = 0, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .pBufferInfo = &camera_buffer_info},
            {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstBinding = 1, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &road_segment_buffer_info},
        };

        cmd_push_descriptor_set_khr(cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, model_3D_pipeline->pipeline_layout, 0, ArrayCount(push_writes), push_writes);
        vkCmdSetDepthBias(cmd_buffer, 0, 0, 0);
        VkShaderStageFlags push_constant_stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        vkCmdPushConstants(cmd_buffer, model_3D_pipeline->pipeline_layout, push_constant_stages, 0, sizeof(TilePipelinePushConstants), &node->push_constants);
        vkCmdBindDescriptorSets(cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, model_3D_pipeline->pipeline_layout, 1, ArrayCount(descriptor_sets), descriptor_sets, 0, NULL);
        vkCmdBindVertexBuffers(cmd_buffer, 0, 1, &node->vertex_alloc.buffer, offsets);
        vkCmdBindIndexBuffer(cmd_buffer, node->index_alloc.buffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdSetDepthWriteEnable(cmd_buffer, VK_TRUE);
        // type of depth compare
        switch (node->depth_compare)
        {
            case render::DepthCompare::LessOrEqual: vkCmdSetDepthCompareOp(cmd_buffer, VK_COMPARE_OP_LESS_OR_EQUAL); break;
            case render::DepthCompare::Equal: vkCmdSetDepthCompareOp(cmd_buffer, VK_COMPARE_OP_EQUAL); break;
            case render::DepthCompare::Always: vkCmdSetDepthCompareOp(cmd_buffer, VK_COMPARE_OP_ALWAYS); break;
            default: vkCmdSetDepthCompareOp(cmd_buffer, VK_COMPARE_OP_LESS); break;
        }
        // color write?
        if (has_flag(node->pipeline_bits, render::TilePipelineBits::ColorDisable))
        {
            VkBool32 color_write_disabled[4] = {};
            cmd_set_color_write_enable_ext(cmd_buffer, ArrayCount(color_write_disabled), color_write_disabled);
        }
        else
        {
            VkBool32 color_write_enabled[4] = {VK_TRUE, VK_TRUE, VK_TRUE, VK_TRUE};
            cmd_set_color_write_enable_ext(cmd_buffer, ArrayCount(color_write_enabled), color_write_enabled);
        }
        // depth write?
        if (has_flag(node->pipeline_bits, render::TilePipelineBits::DepthWriteDisable))
        {
            vkCmdSetDepthWriteEnable(cmd_buffer, VK_FALSE);
        }
        else
        {
            vkCmdSetDepthWriteEnable(cmd_buffer, VK_TRUE);
        }
        vkCmdDrawIndexed(cmd_buffer, node->index_count, 1, node->index_buffer_offset, 0, 0);
    }
}
g_internal Pipeline
blend_3d_pipeline_create(String8 shader_path)
{
    Context* vk_ctx = ctx_get();
    ScratchScope scratch = ScratchScope(0, 0);

    String8 vert_path = CreatePathFromStrings(scratch.arena, Str8BufferFromCString(scratch.arena, {(char*)shader_path.str, "bin", "colormap_blend_3d_vert.spv"}));
    String8 frag_path = CreatePathFromStrings(scratch.arena, Str8BufferFromCString(scratch.arena, {(char*)shader_path.str, "bin", "colormap_blend_3d_frag.spv"}));

    ShaderModuleInfo vert_shader_stage_info = shader_stage_from_spirv(scratch.arena, vk_ctx->device, VK_SHADER_STAGE_VERTEX_BIT, vert_path);
    ShaderModuleInfo frag_shader_stage_info = shader_stage_from_spirv(scratch.arena, vk_ctx->device, VK_SHADER_STAGE_FRAGMENT_BIT, frag_path);

    VkPipelineShaderStageCreateInfo shader_stages[] = {vert_shader_stage_info.info, frag_shader_stage_info.info};

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE, VK_DYNAMIC_STATE_DEPTH_COMPARE_OP,
                                      VK_DYNAMIC_STATE_COLOR_WRITE_ENABLE_EXT};

    VkPipelineDynamicStateCreateInfo dynamic_state{};
    dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount = (U32)(ArrayCount(dynamicStates));
    dynamic_state.pDynamicStates = dynamicStates;

    VkPipelineVertexInputStateCreateInfo vertex_input_info{};
    vertex_input_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    U32 uv_offset = offsetof(render::Vertex3DBlend, uv);
    U32 object_id_offset = offsetof(render::Vertex3DBlend, object_id);
    U32 color_offset = offsetof(render::Vertex3DBlend, blend_factor);

    VkVertexInputAttributeDescription attr_desc[] = {{.location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT},
                                                     {.location = 1, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = uv_offset},
                                                     {.location = 2, .binding = 0, .format = vk_ctx->object_id_format, .offset = object_id_offset},
                                                     {.location = 3, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = color_offset}};

    VkVertexInputBindingDescription input_desc[] = {{.binding = 0, .stride = sizeof(render::Vertex3DBlend), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX}};

    vertex_input_info.vertexBindingDescriptionCount = ArrayCount(input_desc);
    vertex_input_info.vertexAttributeDescriptionCount = ArrayCount(attr_desc);
    vertex_input_info.pVertexBindingDescriptions = input_desc;
    vertex_input_info.pVertexAttributeDescriptions = attr_desc;

    VkPipelineInputAssemblyStateCreateInfo input_assembly{};
    input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (F32)vk_ctx->swapchain_resources->swapchain_extent.width;
    viewport.height = (F32)vk_ctx->swapchain_resources->swapchain_extent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = vk_ctx->swapchain_resources->swapchain_extent;

    VkPipelineViewportStateCreateInfo viewport_state{};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.scissorCount = 1;
    viewport_state.pViewports = &viewport;
    viewport_state.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_TRUE;
    multisampling.rasterizationSamples = vk_ctx->msaa_samples;
    multisampling.minSampleShading = 1.0f;

    VkPipelineColorBlendAttachmentState color_blend_attachment{.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT};

    VkPipelineColorBlendAttachmentState color_blend_attachments[] = {color_blend_attachment, color_blend_attachment};
    VkPipelineColorBlendStateCreateInfo color_blending{};
    color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blending.attachmentCount = ArrayCount(color_blend_attachments);
    color_blending.pAttachments = color_blend_attachments;

    VkDescriptorSetLayout descriptor_set_layouts[] = {vk_ctx->camera_descriptor_set_layout, vk_ctx->bindless_descriptor_set_layout};

    VkPushConstantRange push_constant_range{};
    push_constant_range.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    push_constant_range.offset = 0;
    push_constant_range.size = sizeof(Blend3dPushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = ArrayCount(descriptor_set_layouts);
    pipelineLayoutInfo.pSetLayouts = descriptor_set_layouts;
    pipelineLayoutInfo.pPushConstantRanges = &push_constant_range;
    pipelineLayoutInfo.pushConstantRangeCount = 1;

    VkPipelineDepthStencilStateCreateInfo depth_stencil{};
    depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil.depthTestEnable = VK_TRUE;
    depth_stencil.depthWriteEnable = VK_TRUE;
    depth_stencil.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineLayout pipeline_layout;
    VK_CHECK_RESULT(vkCreatePipelineLayout(vk_ctx->device, &pipelineLayoutInfo, nullptr, &pipeline_layout));

    VkFormat color_attachment_formats[] = {vk_ctx->swapchain_resources->color_format, vk_ctx->swapchain_resources->object_id_image_format};
    VkPipelineRenderingCreateInfo pipeline_rendering_info{};
    pipeline_rendering_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    pipeline_rendering_info.colorAttachmentCount = ArrayCount(color_attachment_formats);
    pipeline_rendering_info.pColorAttachmentFormats = color_attachment_formats;
    pipeline_rendering_info.depthAttachmentFormat = vk_ctx->swapchain_resources->depth_format;

    VkGraphicsPipelineCreateInfo pipeline_create_info{};
    pipeline_create_info.pNext = &pipeline_rendering_info;
    pipeline_create_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_create_info.stageCount = ArrayCount(shader_stages);
    pipeline_create_info.pStages = shader_stages;
    pipeline_create_info.pVertexInputState = &vertex_input_info;
    pipeline_create_info.pInputAssemblyState = &input_assembly;
    pipeline_create_info.pViewportState = &viewport_state;
    pipeline_create_info.pRasterizationState = &rasterizer;
    pipeline_create_info.pMultisampleState = &multisampling;
    pipeline_create_info.pDepthStencilState = &depth_stencil;
    pipeline_create_info.pColorBlendState = &color_blending;
    pipeline_create_info.pDynamicState = &dynamic_state;
    pipeline_create_info.layout = pipeline_layout;

    VkPipeline pipeline;
    VK_CHECK_RESULT(vkCreateGraphicsPipelines(vk_ctx->device, VK_NULL_HANDLE, 1, &pipeline_create_info, nullptr, &pipeline));

    Pipeline pipeline_info = {.pipeline = pipeline, .pipeline_layout = pipeline_layout};
    return pipeline_info;
}

g_internal void
blend_3d_bucket_add(BufferAllocation* vertex_buffer_allocation, BufferAllocation* index_buffer_allocation, render::Handle texture_handle, render::Handle colormap_handle,
                    render::MappedHandle<void> camera_handle)
{
    Context* vk_ctx = ctx_get();
    RenderFrame* render_frame = vk_ctx->render_frame;

    render::AssetItem<TextureHandle>* base_tex = asset_manager_texture_item_get(texture_handle);
    render::AssetItem<TextureHandle>* colormap_tex = asset_manager_texture_item_get(colormap_handle);
    Assert(base_tex);
    Assert(colormap_tex);
    if (!base_tex || !colormap_tex || camera_handle.buffer.size == 0)
    {
        return;
    }

    Blend3DNode* node = PushStruct(vk_ctx->render_frame_arena, Blend3DNode);
    node->vertex_alloc = *vertex_buffer_allocation;
    node->index_alloc = *index_buffer_allocation;
    node->push_constants = {.texture_index = base_tex->item.descriptor_set_idx, .colormap_index = colormap_tex->item.descriptor_set_idx};
    node->camera_handle = camera_handle;

    SLLQueuePush(render_frame->blend_3d_list.first, render_frame->blend_3d_list.last, node);
}
g_internal void
blend_3d_rendering()
{
    Context* vk_ctx = ctx_get();
    VkCommandBuffer cmd_buffer = vk_ctx->command_buffers.data[vk_ctx->current_frame];
    TracyVkZone(vk_ctx->tracy_ctx[vk_ctx->current_frame], cmd_buffer, "blend_3d_rendering");

    Pipeline* blend_3d_pipeline = &vk_ctx->blend_3d_pipeline;
    RenderFrame* render_frame = vk_ctx->render_frame;

    SwapchainResources* swapchain_resources = vk_ctx->swapchain_resources;
    VkExtent2D swapchain_extent = swapchain_resources->swapchain_extent;
    vkCmdBindPipeline(cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, blend_3d_pipeline->pipeline);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (F32)(swapchain_extent.width);
    viewport.height = (F32)(swapchain_extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd_buffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = swapchain_extent;
    vkCmdSetScissor(cmd_buffer, 0, 1, &scissor);

    VkDescriptorSet descriptor_sets[1] = {vk_ctx->bindless_descriptor_set};

    VkDeviceSize offsets[] = {0};
    for (Blend3DNode* node = render_frame->blend_3d_list.first; node; node = node->next)
    {
        render::Handle camera_handle = node->camera_handle.buffer[vk_ctx->current_frame]->handle;
        render::AssetItem<BufferHandle>* camera_buffer_handle = asset_manager_buffer_item_get(camera_handle);
        AssertAlways(camera_buffer_handle);
        BufferHandle* camera_buffer = &camera_buffer_handle->item;

        VkDescriptorBufferInfo camera_buffer_info{};
        camera_buffer_info.buffer = camera_buffer->buffer_alloc.buffer;
        camera_buffer_info.offset = 0;
        camera_buffer_info.range = VK_WHOLE_SIZE;

        VkWriteDescriptorSet push_writes[] = {
            {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstBinding = 0, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .pBufferInfo = &camera_buffer_info},
        };

        cmd_push_descriptor_set_khr(cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, blend_3d_pipeline->pipeline_layout, 0, ArrayCount(push_writes), push_writes);
        vkCmdPushConstants(cmd_buffer, blend_3d_pipeline->pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Blend3dPushConstants), &node->push_constants);
        vkCmdBindDescriptorSets(cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, blend_3d_pipeline->pipeline_layout, 1, ArrayCount(descriptor_sets), descriptor_sets, 0, NULL);
        vkCmdBindVertexBuffers(cmd_buffer, 0, 1, &node->vertex_alloc.buffer, offsets);
        vkCmdBindIndexBuffer(cmd_buffer, node->index_alloc.buffer, 0, VK_INDEX_TYPE_UINT32);

        U32 index_count = node->index_alloc.size / sizeof(U32);
        draw_indexed_separate_depth_and_color_calls(cmd_buffer, 0, index_count, VK_COMPARE_OP_LESS);
    }
}

g_internal Pipeline
primitive_pipeline_create(String8 shader_path)
{
    Context* vk_ctx = ctx_get();
    ScratchScope scratch = ScratchScope(0, 0);

    String8 vert_path = CreatePathFromStrings(scratch.arena, Str8BufferFromCString(scratch.arena, {(char*)shader_path.str, "bin", "primitive_vert.spv"}));
    String8 frag_path = CreatePathFromStrings(scratch.arena, Str8BufferFromCString(scratch.arena, {(char*)shader_path.str, "bin", "primitive_frag.spv"}));

    ShaderModuleInfo vert_shader_stage_info = shader_stage_from_spirv(scratch.arena, vk_ctx->device, VK_SHADER_STAGE_VERTEX_BIT, vert_path);
    ShaderModuleInfo frag_shader_stage_info = shader_stage_from_spirv(scratch.arena, vk_ctx->device, VK_SHADER_STAGE_FRAGMENT_BIT, frag_path);

    VkPipelineShaderStageCreateInfo shader_stages[] = {vert_shader_stage_info.info, frag_shader_stage_info.info};

    VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_COLOR_WRITE_ENABLE_EXT};

    VkPipelineDynamicStateCreateInfo dynamic_state{};
    dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount = ArrayCount(dynamic_states);
    dynamic_state.pDynamicStates = dynamic_states;

    VkPipelineVertexInputStateCreateInfo vertex_input_info{};
    vertex_input_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    U32 pos_offset = offsetof(render::PrimitiveVertex, pos);
    U32 normal_offset = offsetof(render::PrimitiveVertex, normal);
    U32 color_offset = offsetof(render::PrimitiveVertex, color);

    VkVertexInputAttributeDescription attr_desc[] = {
        {.location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = 0},
        {.location = 1, .binding = 1, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = pos_offset},
        {.location = 2, .binding = 1, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = normal_offset},
        {.location = 3, .binding = 1, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = color_offset},
    };

    VkVertexInputBindingDescription input_desc[] = {
        {.binding = 0, .stride = sizeof(glm::vec3), .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE},
        {.binding = 1, .stride = sizeof(render::PrimitiveVertex), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX},
    };

    vertex_input_info.vertexBindingDescriptionCount = ArrayCount(input_desc);
    vertex_input_info.vertexAttributeDescriptionCount = ArrayCount(attr_desc);
    vertex_input_info.pVertexBindingDescriptions = input_desc;
    vertex_input_info.pVertexAttributeDescriptions = attr_desc;

    VkPipelineInputAssemblyStateCreateInfo input_assembly{};
    input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (F32)vk_ctx->swapchain_resources->swapchain_extent.width;
    viewport.height = (F32)vk_ctx->swapchain_resources->swapchain_extent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = vk_ctx->swapchain_resources->swapchain_extent;

    VkPipelineViewportStateCreateInfo viewport_state{};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.scissorCount = 1;
    viewport_state.pViewports = &viewport;
    viewport_state.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = vk_ctx->msaa_samples;
    multisampling.minSampleShading = 0.0f;

    VkPipelineColorBlendAttachmentState color_blend_attachment{.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT};
    VkPipelineColorBlendAttachmentState color_blend_attachments[] = {color_blend_attachment, color_blend_attachment};
    VkPipelineColorBlendStateCreateInfo color_blending{};
    color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blending.attachmentCount = ArrayCount(color_blend_attachments);
    color_blending.pAttachments = color_blend_attachments;

    VkDescriptorSetLayout descriptor_set_layouts[] = {vk_ctx->camera_descriptor_set_layout};

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = ArrayCount(descriptor_set_layouts);
    pipelineLayoutInfo.pSetLayouts = descriptor_set_layouts;

    VkPushConstantRange push_constant_range{};
    push_constant_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    push_constant_range.offset = 0;
    push_constant_range.size = sizeof(F32);
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &push_constant_range;

    VkPipelineDepthStencilStateCreateInfo depth_stencil{};
    depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil.depthTestEnable = VK_TRUE;
    depth_stencil.depthWriteEnable = VK_TRUE;
    depth_stencil.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineLayout pipeline_layout;
    VK_CHECK_RESULT(vkCreatePipelineLayout(vk_ctx->device, &pipelineLayoutInfo, nullptr, &pipeline_layout));

    VkFormat color_attachment_formats[] = {vk_ctx->swapchain_resources->color_format, vk_ctx->swapchain_resources->object_id_image_format};
    VkPipelineRenderingCreateInfo pipeline_rendering_info{};
    pipeline_rendering_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    pipeline_rendering_info.colorAttachmentCount = ArrayCount(color_attachment_formats);
    pipeline_rendering_info.pColorAttachmentFormats = color_attachment_formats;
    pipeline_rendering_info.depthAttachmentFormat = vk_ctx->swapchain_resources->depth_format;

    VkGraphicsPipelineCreateInfo pipeline_create_info{};
    pipeline_create_info.pNext = &pipeline_rendering_info;
    pipeline_create_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_create_info.stageCount = ArrayCount(shader_stages);
    pipeline_create_info.pStages = shader_stages;
    pipeline_create_info.pVertexInputState = &vertex_input_info;
    pipeline_create_info.pInputAssemblyState = &input_assembly;
    pipeline_create_info.pViewportState = &viewport_state;
    pipeline_create_info.pRasterizationState = &rasterizer;
    pipeline_create_info.pMultisampleState = &multisampling;
    pipeline_create_info.pDepthStencilState = &depth_stencil;
    pipeline_create_info.pColorBlendState = &color_blending;
    pipeline_create_info.pDynamicState = &dynamic_state;
    pipeline_create_info.layout = pipeline_layout;

    VkPipeline pipeline;
    VK_CHECK_RESULT(vkCreateGraphicsPipelines(vk_ctx->device, VK_NULL_HANDLE, 1, &pipeline_create_info, nullptr, &pipeline));

    Pipeline pipeline_info = {.pipeline = pipeline, .pipeline_layout = pipeline_layout};
    return pipeline_info;
}

g_internal void
primitive_rendering(Buffer<render::PrimitiveInstance> primitive_instances, render::MappedHandle<void> mapped_camera_handle)
{
    if (primitive_instances.size == 0)
    {
        return;
    }

    Context* vk_ctx = ctx_get();
    VkCommandBuffer cmd_buffer = vk_ctx->command_buffers.data[vk_ctx->current_frame];
    TracyVkZone(vk_ctx->tracy_ctx[vk_ctx->current_frame], cmd_buffer, "primitive_rendering");

    Pipeline* pipeline = &vk_ctx->primitive_pipeline;
    SwapchainResources* swapchain_resources = vk_ctx->swapchain_resources;
    VkExtent2D swapchain_extent = swapchain_resources->swapchain_extent;
    vkCmdBindPipeline(cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (F32)(swapchain_extent.width);
    viewport.height = (F32)(swapchain_extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd_buffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = swapchain_extent;
    vkCmdSetScissor(cmd_buffer, 0, 1, &scissor);

    VkBool32 color_write_enables[] = {VK_TRUE, VK_FALSE};
    cmd_set_color_write_enable_ext(cmd_buffer, ArrayCount(color_write_enables), color_write_enables);

    U64 instance_location_count = 0;
    for (render::PrimitiveInstance& primitive_instance : primitive_instances)
    {
        instance_location_count += primitive_instance.locations.size;
    }
    U64 instance_buffer_byte_count = instance_location_count * sizeof(glm::vec3);
    AssertAlways(instance_buffer_byte_count <= max_U32);
    U32 current_frame = vk_ctx->current_frame;
    render::Handle instance_buffer_handle = buffer_alloc_create_or_resize((U32)instance_buffer_byte_count, vk_ctx->primitive_instance_buffer[current_frame], VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    vk_ctx->primitive_instance_buffer[current_frame] = instance_buffer_handle;

    render::AssetItem<BufferHandle>* instance_buffer_asset = asset_manager_buffer_item_get(instance_buffer_handle);
    AssertAlways(instance_buffer_asset);
    BufferAllocation* instance_buffer_alloc = &instance_buffer_asset->item.buffer_alloc;

    render::Handle camera_handle = mapped_camera_handle.buffer[current_frame]->handle;
    if (!asset_manager_handles_loaded_check({camera_handle}))
    {
        return;
    }

    render::AssetItem<BufferHandle>* camera_buffer_handle = asset_manager_buffer_item_get(camera_handle);
    AssertAlways(camera_buffer_handle);
    BufferHandle* camera_buffer = &camera_buffer_handle->item;

    VkDescriptorBufferInfo camera_buffer_info{};
    camera_buffer_info.buffer = camera_buffer->buffer_alloc.buffer;
    camera_buffer_info.offset = 0;
    camera_buffer_info.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet push_writes[] = {
        {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstBinding = 0, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .pBufferInfo = &camera_buffer_info},
    };
    cmd_push_descriptor_set_khr(cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline_layout, 0, ArrayCount(push_writes), push_writes);

    U32 instance_buffer_offset = 0;
    for (render::PrimitiveInstance& primitive_instance : primitive_instances)
    {
        render::MeshHandle mesh_handle = primitive_instance.mesh_handle;
        if (asset_manager_handles_loaded_check({mesh_handle.index_buffer_handle, mesh_handle.vertex_buffer_handle}))
        {
            render::AssetItem<BufferHandle>* vertex_buffer_asset = asset_manager_buffer_item_get(mesh_handle.vertex_buffer_handle);
            render::AssetItem<BufferHandle>* index_buffer_asset = asset_manager_buffer_item_get(mesh_handle.index_buffer_handle);

            U64 location_buffer_byte_count = primitive_instance.locations.size * sizeof(glm::vec3);
            VK_CHECK_RESULT(
                vmaCopyMemoryToAllocation(vk_ctx->asset_manager->allocator, primitive_instance.locations.data, instance_buffer_alloc->allocation, instance_buffer_offset, location_buffer_byte_count));

            VkBuffer vertex_buffers[] = {instance_buffer_alloc->buffer, vertex_buffer_asset->item.buffer_alloc.buffer};
            VkDeviceSize vertex_offsets[] = {instance_buffer_offset, 0};
            vkCmdPushConstants(cmd_buffer, pipeline->pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(F32), &primitive_instance.scale_factor);
            vkCmdBindVertexBuffers(cmd_buffer, 0, ArrayCount(vertex_buffers), vertex_buffers, vertex_offsets);
            vkCmdBindIndexBuffer(cmd_buffer, index_buffer_asset->item.buffer_alloc.buffer, 0, VK_INDEX_TYPE_UINT16);
            Assert(primitive_instance.locations.size <= max_U32);
            vkCmdDrawIndexed(cmd_buffer, index_buffer_asset->item.elem_count, (U32)primitive_instance.locations.size, 0, 0, 0);
        }
        U64 location_buffer_byte_count = primitive_instance.locations.size * sizeof(glm::vec3);
        instance_buffer_offset += (U32)location_buffer_byte_count;
    }
}

g_internal Pipeline
line_pipeline_create(Context* vk_ctx, String8 shader_path)
{
    ScratchScope scratch = ScratchScope(&vk_ctx->arena, 1);

    String8 vert_path = CreatePathFromStrings(scratch.arena, Str8BufferFromCString(scratch.arena, {(char*)shader_path.str, "bin", "line_vert.spv"}));
    String8 frag_path = CreatePathFromStrings(scratch.arena, Str8BufferFromCString(scratch.arena, {(char*)shader_path.str, "bin", "line_frag.spv"}));

    ShaderModuleInfo vert_shader_stage_info = shader_stage_from_spirv(scratch.arena, vk_ctx->device, VK_SHADER_STAGE_VERTEX_BIT, vert_path);
    ShaderModuleInfo frag_shader_stage_info = shader_stage_from_spirv(scratch.arena, vk_ctx->device, VK_SHADER_STAGE_FRAGMENT_BIT, frag_path);
    VkPipelineShaderStageCreateInfo shader_stages[] = {vert_shader_stage_info.info, frag_shader_stage_info.info};

    VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_COLOR_WRITE_ENABLE_EXT};
    VkPipelineDynamicStateCreateInfo dynamic_state{};
    dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount = ArrayCount(dynamic_states);
    dynamic_state.pDynamicStates = dynamic_states;

    VkVertexInputAttributeDescription attribute_descriptions[] = {
        {.location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(render::LineVertex, pos)},
        {.location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(render::LineVertex, color)},
    };
    VkVertexInputBindingDescription binding_description = {.binding = 0, .stride = sizeof(render::LineVertex), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX};
    VkPipelineVertexInputStateCreateInfo vertex_input_info{};
    vertex_input_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input_info.vertexBindingDescriptionCount = 1;
    vertex_input_info.pVertexBindingDescriptions = &binding_description;
    vertex_input_info.vertexAttributeDescriptionCount = ArrayCount(attribute_descriptions);
    vertex_input_info.pVertexAttributeDescriptions = attribute_descriptions;

    VkPipelineInputAssemblyStateCreateInfo input_assembly{};
    input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;

    VkPipelineViewportStateCreateInfo viewport_state{};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = vk_ctx->msaa_samples;

    VkPipelineColorBlendAttachmentState color_blend_attachment = {
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };
    VkPipelineColorBlendAttachmentState color_blend_attachments[] = {color_blend_attachment, color_blend_attachment};
    VkPipelineColorBlendStateCreateInfo color_blending{};
    color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blending.attachmentCount = ArrayCount(color_blend_attachments);
    color_blending.pAttachments = color_blend_attachments;

    VkDescriptorSetLayout descriptor_set_layouts[] = {vk_ctx->camera_descriptor_set_layout};
    VkPipelineLayoutCreateInfo pipeline_layout_info{};
    pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_info.setLayoutCount = ArrayCount(descriptor_set_layouts);
    pipeline_layout_info.pSetLayouts = descriptor_set_layouts;

    VkPipelineDepthStencilStateCreateInfo depth_stencil{};
    depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil.depthTestEnable = VK_TRUE;
    depth_stencil.depthWriteEnable = VK_FALSE;
    depth_stencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VK_CHECK_RESULT(vkCreatePipelineLayout(vk_ctx->device, &pipeline_layout_info, nullptr, &pipeline_layout));

    VkFormat color_attachment_formats[] = {vk_ctx->swapchain_resources->color_format, vk_ctx->swapchain_resources->object_id_image_format};
    VkPipelineRenderingCreateInfo pipeline_rendering_info{};
    pipeline_rendering_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    pipeline_rendering_info.colorAttachmentCount = ArrayCount(color_attachment_formats);
    pipeline_rendering_info.pColorAttachmentFormats = color_attachment_formats;
    pipeline_rendering_info.depthAttachmentFormat = vk_ctx->swapchain_resources->depth_format;

    VkGraphicsPipelineCreateInfo pipeline_create_info{};
    pipeline_create_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_create_info.pNext = &pipeline_rendering_info;
    pipeline_create_info.stageCount = ArrayCount(shader_stages);
    pipeline_create_info.pStages = shader_stages;
    pipeline_create_info.pVertexInputState = &vertex_input_info;
    pipeline_create_info.pInputAssemblyState = &input_assembly;
    pipeline_create_info.pViewportState = &viewport_state;
    pipeline_create_info.pRasterizationState = &rasterizer;
    pipeline_create_info.pMultisampleState = &multisampling;
    pipeline_create_info.pDepthStencilState = &depth_stencil;
    pipeline_create_info.pColorBlendState = &color_blending;
    pipeline_create_info.pDynamicState = &dynamic_state;
    pipeline_create_info.layout = pipeline_layout;

    VkPipeline pipeline = VK_NULL_HANDLE;
    VK_CHECK_RESULT(vkCreateGraphicsPipelines(vk_ctx->device, VK_NULL_HANDLE, 1, &pipeline_create_info, nullptr, &pipeline));

    Pipeline result = {.pipeline = pipeline, .pipeline_layout = pipeline_layout};
    return result;
}

// Line rendering
g_internal void
lines_render(Buffer<render::LineVertex> line_vertices, render::MappedHandle<void> mapped_camera_handle)
{
    Context* vk_ctx = ctx_get();
    VkCommandBuffer cmd_buffer = vk_ctx->command_buffers.data[vk_ctx->current_frame];
    TracyVkZone(vk_ctx->tracy_ctx[vk_ctx->current_frame], cmd_buffer, "line_rendering");

    Pipeline* pipeline = &vk_ctx->line_pipeline;

    SwapchainResources* swapchain_resources = vk_ctx->swapchain_resources;
    VkExtent2D swapchain_extent = swapchain_resources->swapchain_extent;
    vkCmdBindPipeline(cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (F32)(swapchain_extent.width);
    viewport.height = (F32)(swapchain_extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd_buffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = swapchain_extent;
    vkCmdSetScissor(cmd_buffer, 0, 1, &scissor);

    Assert(line_vertices.size % 2 == 0);
    Assert(line_vertices.size <= max_U32);
    U64 line_buffer_byte_count = line_vertices.size * sizeof(render::LineVertex);
    Assert(line_buffer_byte_count <= max_U32);
    VkBool32 color_write_enables[] = {VK_TRUE, VK_FALSE};
    cmd_set_color_write_enable_ext(cmd_buffer, ArrayCount(color_write_enables), color_write_enables);
    U32 current_frame = vk_ctx->current_frame;
    render::Handle line_buffer_handle = buffer_alloc_create_or_resize((U32)line_buffer_byte_count, vk_ctx->line_buffer_handle[current_frame], VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    vk_ctx->line_buffer_handle[current_frame] = line_buffer_handle;

    render::AssetItem<BufferHandle>* line_buffer_asset = asset_manager_buffer_item_get(line_buffer_handle);
    AssertAlways(line_buffer_asset);
    BufferAllocation* buffer_alloc = &line_buffer_asset->item.buffer_alloc;

    render::Handle camera_handle = mapped_camera_handle.buffer[vk_ctx->current_frame]->handle;
    if (asset_manager_handles_loaded_check({camera_handle}))
    {
        render::AssetItem<BufferHandle>* camera_buffer_handle = asset_manager_buffer_item_get(camera_handle);
        AssertAlways(camera_buffer_handle);
        BufferHandle* camera_buffer = &camera_buffer_handle->item;

        VkDescriptorBufferInfo camera_buffer_info{};
        camera_buffer_info.buffer = camera_buffer->buffer_alloc.buffer;
        camera_buffer_info.offset = 0;
        camera_buffer_info.range = VK_WHOLE_SIZE;

        VkWriteDescriptorSet push_writes[] = {
            {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstBinding = 0, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .pBufferInfo = &camera_buffer_info},
        };

        VK_CHECK_RESULT(vmaCopyMemoryToAllocation(vk_ctx->asset_manager->allocator, line_vertices.data, buffer_alloc->allocation, 0, line_buffer_byte_count));

        VkBuffer vertex_buffers[] = {buffer_alloc->buffer};
        VkDeviceSize vertex_offsets[] = {0};
        cmd_push_descriptor_set_khr(cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline_layout, 0, ArrayCount(push_writes), push_writes);
        vkCmdBindVertexBuffers(cmd_buffer, 0, ArrayCount(vertex_buffers), vertex_buffers, vertex_offsets);
        vkCmdDraw(cmd_buffer, (U32)line_vertices.size, 1, 0, 0);
    }
}

} // namespace vulkan

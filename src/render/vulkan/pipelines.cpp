namespace vulkan
{
static Pipeline
car_instance_pipeline_create(Context* vk_ctx, String8 shader_path)
{
    ScratchScope scratch = ScratchScope(0, 0);

    String8 vert_path = CreatePathFromStrings(scratch.arena, Str8BufferFromCString(scratch.arena, {(char*)shader_path.str, "bin", "model_3d_instancing_vert.spv"}));
    String8 frag_path = CreatePathFromStrings(scratch.arena, Str8BufferFromCString(scratch.arena, {(char*)shader_path.str, "bin", "model_3d_instancing_frag.spv"}));

    ShaderModuleInfo vert_shader_stage_info = shader_stage_from_spirv(scratch.arena, vk_ctx->device, VK_SHADER_STAGE_VERTEX_BIT, vert_path);
    ShaderModuleInfo frag_shader_stage_info = shader_stage_from_spirv(scratch.arena, vk_ctx->device, VK_SHADER_STAGE_FRAGMENT_BIT, frag_path);

    VkPipelineShaderStageCreateInfo shader_stages[] = {vert_shader_stage_info.info, frag_shader_stage_info.info};

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

    VkPipelineDynamicStateCreateInfo dynamic_state{};
    dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount = (U32)(ArrayCount(dynamicStates));
    dynamic_state.pDynamicStates = dynamicStates;

    VkPipelineVertexInputStateCreateInfo vertex_input_info{};
    vertex_input_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    U32 uv_offset = (U32)offsetof(render::TileVertex, uv);
    U32 x_basis_offset = (U32)offsetof(render::Model3DInstance, x_basis);
    U32 y_basis_offset = (U32)offsetof(render::Model3DInstance, y_basis);
    U32 z_basis_offset = (U32)offsetof(render::Model3DInstance, z_basis);
    U32 w_basis_offset = (U32)offsetof(render::Model3DInstance, w_basis);

    VkVertexInputAttributeDescription attr_desc[] = {
        {.location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT},
        {.location = 1, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = uv_offset},
        {.location = 2, .binding = 1, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = x_basis_offset},
        {.location = 3, .binding = 1, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = y_basis_offset},
        {.location = 4, .binding = 1, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = z_basis_offset},
        {.location = 5, .binding = 1, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = w_basis_offset},
    };
    VkVertexInputBindingDescription input_desc[] = {{.binding = 0, .stride = sizeof(render::TileVertex), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX},
                                                    {.binding = 1, .stride = sizeof(render::Model3DInstance), .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE}};

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

static Pipeline
model_3d_pipeline_create(Context* vk_ctx, String8 shader_path)
{
    ScratchScope scratch = ScratchScope(0, 0);

    String8 vert_path = CreatePathFromStrings(scratch.arena, Str8BufferFromCString(scratch.arena, {(char*)shader_path.str, "bin", "model_3d_vert.spv"}));
    String8 frag_path = CreatePathFromStrings(scratch.arena, Str8BufferFromCString(scratch.arena, {(char*)shader_path.str, "bin", "model_3d_frag.spv"}));

    ShaderModuleInfo vert_shader_stage_info = shader_stage_from_spirv(scratch.arena, vk_ctx->device, VK_SHADER_STAGE_VERTEX_BIT, vert_path);
    ShaderModuleInfo frag_shader_stage_info = shader_stage_from_spirv(scratch.arena, vk_ctx->device, VK_SHADER_STAGE_FRAGMENT_BIT, frag_path);

    VkPipelineShaderStageCreateInfo shader_stages[] = {vert_shader_stage_info.info, frag_shader_stage_info.info};

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
    U32 colormap_value_offset = offsetof(render::TileVertex, colormap_value);
    U32 uv_offset = offsetof(render::TileVertex, uv);
    U32 overlay_uv_offset = offsetof(render::TileVertex, overlay_uv);
    U32 object_id_offset = offsetof(render::TileVertex, object_id);

    VkVertexInputAttributeDescription attr_desc[] = {{.location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = pos_offset},
                                                     {.location = 1, .binding = 0, .format = VK_FORMAT_R32_SFLOAT, .offset = colormap_value_offset},
                                                     {.location = 2, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = uv_offset},
                                                     {.location = 3, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = overlay_uv_offset},
                                                     {.location = 4, .binding = 0, .format = vk_ctx->object_id_format, .offset = object_id_offset}};

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
    rasterizer.lineWidth = 1.0f;
    rasterizer.depthBiasEnable = VK_TRUE;

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

static Pipeline
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

static Pipeline
road_intersection_pipeline_create(String8 shader_path)
{
    Context* vk_ctx = ctx_get();
    ScratchScope scratch = ScratchScope(0, 0);

    String8 comp_path = CreatePathFromStrings(scratch.arena, Str8BufferFromCString(scratch.arena, {(char*)shader_path.str, "bin", "road_intersection_comp.spv"}));

    ShaderModuleInfo comp_shader_stage_info = shader_stage_from_spirv(scratch.arena, vk_ctx->device, VK_SHADER_STAGE_COMPUTE_BIT, comp_path);

    VkPushConstantRange push_constant_range{};
    push_constant_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    push_constant_range.offset = 0;
    push_constant_range.size = sizeof(RoadIntersectionPushConstants);

    // Set 0: Road segments, road segment nodes, vertex buffer, index buffer.
    VkDescriptorSetLayoutBinding storage_buffer_bindings[] = {
        {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
        {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
        {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
    };

    VkDescriptorSetLayoutCreateInfo storage_buffer_layout_info{};
    storage_buffer_layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    storage_buffer_layout_info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
    storage_buffer_layout_info.bindingCount = ArrayCount(storage_buffer_bindings);
    storage_buffer_layout_info.pBindings = storage_buffer_bindings;

    VK_CHECK_RESULT(vkCreateDescriptorSetLayout(vk_ctx->device, &storage_buffer_layout_info, nullptr, &vk_ctx->storage_buffer_descriptor_set_layout));

    VkDescriptorSetLayout set_layouts[] = {vk_ctx->storage_buffer_descriptor_set_layout};

    VkPipelineLayoutCreateInfo pipeline_layout_info{};
    pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_info.setLayoutCount = ArrayCount(set_layouts);
    pipeline_layout_info.pSetLayouts = set_layouts;
    pipeline_layout_info.pushConstantRangeCount = 1;
    pipeline_layout_info.pPushConstantRanges = &push_constant_range;

    VkPipelineLayout pipeline_layout;
    VK_CHECK_RESULT(vkCreatePipelineLayout(vk_ctx->device, &pipeline_layout_info, nullptr, &pipeline_layout));

    VkComputePipelineCreateInfo pipeline_create_info{};
    pipeline_create_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipeline_create_info.stage = comp_shader_stage_info.info;
    pipeline_create_info.layout = pipeline_layout;

    VkPipeline pipeline;
    VK_CHECK_RESULT(vkCreateComputePipelines(vk_ctx->device, VK_NULL_HANDLE, 1, &pipeline_create_info, nullptr, &pipeline));

    Pipeline pipeline_info = {.pipeline = pipeline, .pipeline_layout = pipeline_layout};
    return pipeline_info;
}

static Pipeline
car_instance_compute_pipeline_create(String8 shader_path)
{
    Context* vk_ctx = ctx_get();
    ScratchScope scratch = ScratchScope(0, 0);

    String8 comp_path = CreatePathFromStrings(scratch.arena, Str8BufferFromCString(scratch.arena, {(char*)shader_path.str, "bin", "car_height_calculate_comp.spv"}));

    ShaderModuleInfo comp_shader_stage_info = shader_stage_from_spirv(scratch.arena, vk_ctx->device, VK_SHADER_STAGE_COMPUTE_BIT, comp_path);

    VkPushConstantRange push_constant_range{};
    push_constant_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    push_constant_range.offset = 0;
    push_constant_range.size = sizeof(CarHeightCalculatePushConstants);

    VkDescriptorSetLayoutBinding bindings[] = {
        {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
        {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
    };

    VkDescriptorSetLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
    layout_info.bindingCount = ArrayCount(bindings);
    layout_info.pBindings = bindings;

    VK_CHECK_RESULT(vkCreateDescriptorSetLayout(vk_ctx->device, &layout_info, nullptr, &vk_ctx->car_height_calculate_descriptor_set_layout));

    VkPipelineLayoutCreateInfo pipeline_layout_info{};
    pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_info.setLayoutCount = 1;
    pipeline_layout_info.pSetLayouts = &vk_ctx->car_height_calculate_descriptor_set_layout;
    pipeline_layout_info.pushConstantRangeCount = 1;
    pipeline_layout_info.pPushConstantRanges = &push_constant_range;

    VkPipelineLayout pipeline_layout;
    VK_CHECK_RESULT(vkCreatePipelineLayout(vk_ctx->device, &pipeline_layout_info, nullptr, &pipeline_layout));

    VkComputePipelineCreateInfo pipeline_create_info{};
    pipeline_create_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipeline_create_info.stage = comp_shader_stage_info.info;
    pipeline_create_info.layout = pipeline_layout;

    VkPipeline pipeline;
    VK_CHECK_RESULT(vkCreateComputePipelines(vk_ctx->device, VK_NULL_HANDLE, 1, &pipeline_create_info, nullptr, &pipeline));

    Pipeline pipeline_info = {.pipeline = pipeline, .pipeline_layout = pipeline_layout};
    return pipeline_info;
}

} // namespace vulkan

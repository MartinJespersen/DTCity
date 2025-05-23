internal void
TerrainAllocations(Arena* arena, Terrain* terrain, U32 frames_in_flight)
{
    terrain->descriptor_sets = push_array(arena, VkDescriptorSet, frames_in_flight);

    terrain->buffer = push_array(arena, VkBuffer, frames_in_flight);
    terrain->buffer_memory = push_array(arena, VkDeviceMemory, frames_in_flight);
    terrain->buffer_memory_mapped = push_array(arena, void*, frames_in_flight);
}

internal void
TerrainDescriptorPoolCreate(Terrain* terrain, U32 frames_in_flight)
{
    VulkanContext* vk_ctx = GlobalContextGet()->vulkanContext;

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSize.descriptorCount = frames_in_flight;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;

    poolInfo.maxSets = frames_in_flight;

    if (vkCreateDescriptorPool(vk_ctx->device, &poolInfo, nullptr, &terrain->descriptor_pool) !=
        VK_SUCCESS)
    {
        exitWithError("failed to create descriptor pool!");
    }
}

internal void
TerrainDescriptorSetLayoutCreate(VkDevice device, Terrain* terrain)
{
    VkDescriptorSetLayoutBinding terrain_desc_layout{};
    terrain_desc_layout.binding = 0;
    terrain_desc_layout.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    terrain_desc_layout.descriptorCount = 1;
    terrain_desc_layout.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &terrain_desc_layout;

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr,
                                    &terrain->descriptor_set_layout) != VK_SUCCESS)
    {
        exitWithError("failed to create descriptor set layout!");
    }
}

internal void
TerrainDescriptorSetCreate(Terrain* terrain, U32 frames_in_flight)
{
    Temp scratch = ArenaScratchGet();
    Arena* arena = scratch.arena;

    VulkanContext* vk_ctx = GlobalContextGet()->vulkanContext;

    Buffer<VkDescriptorSetLayout> layouts =
        BufferAlloc<VkDescriptorSetLayout>(arena, frames_in_flight);

    for (U32 i = 0; i < layouts.size; i++)
    {
        layouts.data[i] = terrain->descriptor_set_layout;
    }

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = terrain->descriptor_pool;
    allocInfo.descriptorSetCount = layouts.size;
    allocInfo.pSetLayouts = layouts.data;

    if (vkAllocateDescriptorSets(vk_ctx->device, &allocInfo, terrain->descriptor_sets) !=
        VK_SUCCESS)
    {
        exitWithError("failed to allocate descriptor sets!");
    }

    for (size_t i = 0; i < frames_in_flight; i++)
    {
        VkDescriptorBufferInfo buffer_info{};
        buffer_info.buffer = terrain->buffer[i];
        buffer_info.offset = 0;
        buffer_info.range = sizeof(TerrainTransform);

        VkWriteDescriptorSet descriptor_write{};
        descriptor_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptor_write.dstSet = terrain->descriptor_sets[i];
        descriptor_write.dstBinding = 0;
        descriptor_write.dstArrayElement = 0;
        descriptor_write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptor_write.descriptorCount = 1;
        descriptor_write.pBufferInfo = &buffer_info;
        descriptor_write.pImageInfo = nullptr;       // Optional
        descriptor_write.pTexelBufferView = nullptr; // Optional

        vkUpdateDescriptorSets(vk_ctx->device, 1, &descriptor_write, 0, nullptr);
    }

    scratch_end(scratch);
}

internal void
TerrainUniformBufferCreate(Terrain* terrain, U32 frames_in_flight)
{
    VulkanContext* vk_ctx = GlobalContextGet()->vulkanContext;
    VkDeviceSize terrain_buffer_size = sizeof(TerrainTransform);

    for (U32 i = 0; i < frames_in_flight; i++)
    {
        BufferCreate(vk_ctx->physicalDevice, vk_ctx->device, terrain_buffer_size,
                     VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     &terrain->buffer[i], &terrain->buffer_memory[i]);

        vkMapMemory(vk_ctx->device, terrain->buffer_memory[i], 0, terrain_buffer_size, 0,
                    &terrain->buffer_memory_mapped[i]);
    }
}

String8
create_path_from_strings(Arena* arena, String8List* path_list, char** parts, U64 count)
{
    StringJoin join_params = {.sep = str8_cstring("\\")}; // Default to Unix-style separator

    // Step 1: Convert each char* to String8 and push to list
    for (U64 i = 0; i < count; i++)
    {
        String8 part = str8_cstring(parts[i]);
        str8_list_push(arena, path_list, part);
    }

    String8 result = str8_list_join(arena, path_list, &join_params);
    return result;
}

internal void
TerrainGraphicsPipelineCreate(Terrain* terrain)
{
    Temp scratch = scratch_begin(0, 0);

    VulkanContext* vk_ctx = GlobalContextGet()->vulkanContext;

    String8List vert_path_list = {0};
    String8List frag_path_list = {0};
    String8 cwd = os_get_current_path(scratch.arena);
    str8_list_push(scratch.arena, &vert_path_list, cwd);
    str8_list_push(scratch.arena, &frag_path_list, cwd);

    const char* vertex_path_strs[] = {"shaders", "terrain.vert"};
    String8 vertex_path_abs = create_path_from_strings(
        scratch.arena, &vert_path_list, (char**)vertex_path_strs, ArrayCount(vertex_path_strs));

    const char* fragment_path_strings[] = {"shaders", "terrain.frag"};
    String8 fragment_path_abs = create_path_from_strings(
        scratch.arena, &frag_path_list, (char**)vertex_path_strs, ArrayCount(vertex_path_strs));

    Buffer<U8> vert_shader_buffer = IO_ReadFile(scratch.arena, vertex_path_abs);
    Buffer<U8> frag_shader_buffer = IO_ReadFile(scratch.arena, fragment_path_abs);

    VkShaderModule vert_shader_module = ShaderModuleCreate(vk_ctx->device, vert_shader_buffer);
    VkShaderModule frag_shader_module = ShaderModuleCreate(vk_ctx->device, frag_shader_buffer);

    VkPipelineShaderStageCreateInfo vert_shader_stage_info{};
    vert_shader_stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vert_shader_stage_info.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vert_shader_stage_info.module = vert_shader_module;
    vert_shader_stage_info.pName = "main";

    VkPipelineShaderStageCreateInfo frag_shader_stage_info{};
    frag_shader_stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    frag_shader_stage_info.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    frag_shader_stage_info.module = frag_shader_module;
    frag_shader_stage_info.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vert_shader_stage_info,
                                                      frag_shader_stage_info};

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = (U32)(ArrayCount(dynamicStates));
    dynamicState.pDynamicStates = dynamicStates;

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 0;
    vertexInputInfo.pVertexBindingDescriptions = nullptr; // Optional
    vertexInputInfo.vertexAttributeDescriptionCount = 0;
    vertexInputInfo.pVertexAttributeDescriptions = nullptr; // Optional

    Buffer<VkVertexInputAttributeDescription> attr_desc =
        TerrainAttributeDescriptionGet(scratch.arena);
    VkVertexInputBindingDescription input_desc = TerrainBindingDescriptionGet();
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.vertexAttributeDescriptionCount = attr_desc.size;
    vertexInputInfo.pVertexBindingDescriptions = &input_desc;
    vertexInputInfo.pVertexAttributeDescriptions = attr_desc.data;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = vk_ctx->swapChainExtent.width;
    viewport.height = vk_ctx->swapChainExtent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;
    viewportState.pViewports = &viewport;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;
    rasterizer.depthBiasEnable = VK_FALSE;
    rasterizer.depthBiasConstantFactor = 0.0f; // Optional
    rasterizer.depthBiasClamp = 0.0f;          // Optional
    rasterizer.depthBiasSlopeFactor = 0.0f;    // Optional

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_TRUE;
    multisampling.rasterizationSamples = vk_ctx->msaaSamples;
    multisampling.minSampleShading = 1.0f;          // Optional
    multisampling.pSampleMask = nullptr;            // Optional
    multisampling.alphaToCoverageEnable = VK_FALSE; // Optional
    multisampling.alphaToOneEnable = VK_FALSE;      // Optional

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;  // Optional
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO; // Optional
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;             // Optional
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;  // Optional
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO; // Optional
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;             // Optional
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.logicOp = VK_LOGIC_OP_COPY; // Optional
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;
    colorBlending.blendConstants[0] = 0.0f; // Optional
    colorBlending.blendConstants[1] = 0.0f; // Optional
    colorBlending.blendConstants[2] = 0.0f; // Optional
    colorBlending.blendConstants[3] = 0.0f; // Optional

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &terrain->descriptor_set_layout;
    pipelineLayoutInfo.pushConstantRangeCount = 0;    // Optional
    pipelineLayoutInfo.pPushConstantRanges = nullptr; // Optional

    if (vkCreatePipelineLayout(vk_ctx->device, &pipelineLayoutInfo, nullptr,
                               &terrain->vk_pipeline_layout) != VK_SUCCESS)
    {
        exitWithError("failed to create pipeline layout!");
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = nullptr; // Optional
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState; // Optional
    pipelineInfo.layout = terrain->vk_pipeline_layout;
    pipelineInfo.renderPass = terrain->vk_renderpass;
    pipelineInfo.subpass = 0;

    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE; // Optional
    pipelineInfo.basePipelineIndex = -1;              // Optional

    if (vkCreateGraphicsPipelines(vk_ctx->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr,
                                  &terrain->vk_pipeline) != VK_SUCCESS)
    {
        exitWithError("failed to create graphics pipeline!");
    }

    vkDestroyShaderModule(vk_ctx->device, frag_shader_module, nullptr);
    vkDestroyShaderModule(vk_ctx->device, vert_shader_module, nullptr);

    scratch_end(scratch);
    return;
}

internal void
TerrainVulkanCleanup(Terrain* terrain, U32 frames_in_flight)
{
    VulkanContext* vk_ctx = GlobalContextGet()->vulkanContext;

    for (size_t i = 0; i < frames_in_flight; i++)
    {
        vkDestroyBuffer(vk_ctx->device, terrain->buffer[i], nullptr);
        vkFreeMemory(vk_ctx->device, terrain->buffer_memory[i], nullptr);
    }

    vkDestroyDescriptorPool(vk_ctx->device, terrain->descriptor_pool, nullptr);

    vkDestroyDescriptorSetLayout(vk_ctx->device, terrain->descriptor_set_layout, nullptr);
    vkDestroyPipelineLayout(vk_ctx->device, terrain->vk_pipeline_layout, nullptr);
    vkDestroyRenderPass(vk_ctx->device, terrain->vk_renderpass, nullptr);
}

internal void
UpdateTerrainTransform(Terrain* terrain, Vec2F32 screen_res, U32 current_image)
{
    terrain->transform.model =
        glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    terrain->transform.view = glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f),
                                          glm::vec3(0.0f, 0.0f, 1.0f));
    terrain->transform.proj =
        glm::perspective(glm::radians(45.0f), screen_res.x / screen_res.y, 0.1f, 10.0f);
    terrain->transform.proj[1][1] *= -1;

    MemoryCopy(terrain->buffer[current_image], &terrain->transform, sizeof(TerrainTransform));
}

internal void
TerrainRenderPassCreate(Terrain* terrain)
{
    VulkanContext* vk_ctx = GlobalContextGet()->vulkanContext;
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = vk_ctx->swapChainImageFormat;
    colorAttachment.samples = vk_ctx->msaaSamples;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription colorAttachmentResolve{};
    colorAttachmentResolve.format = vk_ctx->swapChainImageFormat;
    colorAttachmentResolve.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachmentResolve.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachmentResolve.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachmentResolve.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachmentResolve.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachmentResolve.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachmentResolve.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout =
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; // This one is optimal for color
                                                  // attachment for writing colors
                                                  // from fragment shader

    VkAttachmentReference colorAttachmentResolveRef{};
    colorAttachmentResolveRef.attachment = 1;
    colorAttachmentResolveRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pResolveAttachments = &colorAttachmentResolveRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;

    const U32 attachmentsCount = 2;
    VkAttachmentDescription attachments[attachmentsCount] = {colorAttachment,
                                                             colorAttachmentResolve};

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = attachmentsCount;
    renderPassInfo.pAttachments = attachments;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    if (vkCreateRenderPass(vk_ctx->device, &renderPassInfo, nullptr, &terrain->vk_renderpass) !=
        VK_SUCCESS)
    {
        exitWithError("failed to create render pass!");
    }
}

internal void
TerrainRenderPassBegin(Terrain* terrain, U32 image_index, U32 current_frame)
{
    VulkanContext* vk_ctx = GlobalContextGet()->vulkanContext;

    VkExtent2D swap_chain_extent = vk_ctx->swapChainExtent;
    VkCommandBuffer command_buffer = vk_ctx->commandBuffers.data[current_frame];

    VkRenderPassBeginInfo renderpass_info{};
    renderpass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderpass_info.renderPass = terrain->vk_renderpass;
    renderpass_info.framebuffer = vk_ctx->swapChainFramebuffers.data[image_index];
    renderpass_info.renderArea.offset = {0, 0};
    renderpass_info.renderArea.extent = swap_chain_extent;

    const U32 clearValueCount = 2;
    VkClearValue clearValues[clearValueCount] = {0};
    clearValues[0].color = {{0.0f, 0.0f, 0.0f, 0.0f}};
    clearValues[1].depthStencil = {1.0f, 0};

    renderpass_info.clearValueCount = clearValueCount;
    renderpass_info.pClearValues = &clearValues[0];

    vkCmdBeginRenderPass(command_buffer, &renderpass_info, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, terrain->vk_pipeline);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(swap_chain_extent.width);
    viewport.height = static_cast<float>(swap_chain_extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(command_buffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = swap_chain_extent;
    vkCmdSetScissor(command_buffer, 0, 1, &scissor);

    VkBuffer vertexBuffers[] = {terrain->vk_vertex_buffer};
    VkDeviceSize offsets[] = {0};
    F32 resolutionData[2] = {(F32)swap_chain_extent.width, (F32)swap_chain_extent.height};
    vkCmdBindVertexBuffers(command_buffer, 0, 1, vertexBuffers, offsets);

    vkCmdBindIndexBuffer(command_buffer, terrain->vk_index_buffer, 0, VK_INDEX_TYPE_UINT16);

    vkCmdDrawIndexed(command_buffer, terrain->index_count, 0, 0, 0, 0);

    vkCmdEndRenderPass(command_buffer);
}

internal void
TerrainInit()
{
    Context* ctx = GlobalContextGet();
    VulkanContext* vk_ctx = ctx->vulkanContext;

    ctx->terrain = push_struct(ctx->arena_permanent, Terrain);
    TerrainAllocations(vk_ctx->arena, ctx->terrain, vk_ctx->MAX_FRAMES_IN_FLIGHT);
    TerrainDescriptorPoolCreate(ctx->terrain, vk_ctx->MAX_FRAMES_IN_FLIGHT);
    TerrainDescriptorSetLayoutCreate(vk_ctx->device, ctx->terrain);
    TerrainUniformBufferCreate(ctx->terrain, vk_ctx->MAX_FRAMES_IN_FLIGHT);
    TerrainDescriptorSetCreate(ctx->terrain, vk_ctx->MAX_FRAMES_IN_FLIGHT);
    TerrainRenderPassCreate(ctx->terrain);
    TerrainGraphicsPipelineCreate(ctx->terrain);
}

internal VkVertexInputBindingDescription
TerrainBindingDescriptionGet()
{
    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(TerrainVertex);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
    return bindingDescription;
}

internal Buffer<VkVertexInputAttributeDescription>
TerrainAttributeDescriptionGet(Arena* arena)
{
    Buffer<VkVertexInputAttributeDescription> attribute_descriptions =
        BufferAlloc<VkVertexInputAttributeDescription>(arena, 1);
    attribute_descriptions.data[0].binding = 0;
    attribute_descriptions.data[0].location = 0;
    attribute_descriptions.data[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attribute_descriptions.data[0].offset = offsetof(TerrainVertex, pos);

    return attribute_descriptions;
}
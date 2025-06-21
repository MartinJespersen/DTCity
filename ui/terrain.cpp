
internal String8
CreatePathFromStrings(Arena* arena, char** parts, U64 count)
{
    String8List path_list = {0};
    StringJoin join_params = {.sep = str8_cstring("\\")}; // Default to Unix-style separator

    // Step 1: Convert each char* to String8 and push to list
    for (U64 i = 0; i < count; i++)
    {
        String8 part = str8_cstring(parts[i]);
        str8_list_push(arena, &path_list, part);
    }

    String8 result = str8_list_join(arena, &path_list, &join_params);
    return result;
}

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

    VkDescriptorPoolSize pool_sizes[] = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, frames_in_flight},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, frames_in_flight},
    };
    U32 pool_size_count = ArrayCount(pool_sizes);

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = pool_size_count;
    poolInfo.pPoolSizes = pool_sizes;

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

    VkDescriptorSetLayoutBinding sampler_layout_binding{};
    sampler_layout_binding.binding = 1;
    sampler_layout_binding.descriptorCount = 1;
    sampler_layout_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    sampler_layout_binding.stageFlags =
        VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT | VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;

    VkDescriptorSetLayoutBinding descriptor_layout_bindings[] = {terrain_desc_layout,
                                                                 sampler_layout_binding};

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = ArrayCount(descriptor_layout_bindings);
    layoutInfo.pBindings = descriptor_layout_bindings;

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr,
                                    &terrain->descriptor_set_layout) != VK_SUCCESS)
    {
        exitWithError("failed to create descriptor set layout!");
    }
}

internal void
TerrainTextureResourceCreate(VulkanContext* vk_ctx, Terrain* terrain, const char* cwd)
{
    Temp scratch = scratch_begin(0, 0);
    VkBuffer vk_texture_staging_buffer;
    VkDeviceMemory vk_texture_staging_buffer_memory;

    // check for blitting format
    terrain->vk_texture_blit_format = VK_FORMAT_R8G8B8A8_SRGB;
    VkFormatProperties formatProperties;
    vkGetPhysicalDeviceFormatProperties(vk_ctx->physical_device, terrain->vk_texture_blit_format,
                                        &formatProperties);
    if (!(formatProperties.optimalTilingFeatures &
          VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))
    {
        throw std::runtime_error("texture image format does not support linear blitting!");
    }

    // load heightmap
    String8List heightmap_path_list = {0};
    String8 cwd_str = str8_cstring(cwd);
    str8_list_push(scratch.arena, &heightmap_path_list, cwd_str);
    const char* heightmap_file_path[] = {cwd, "textures", "heightmap.png"};
    String8 heightmap_path_abs = CreatePathFromStrings(scratch.arena, (char**)heightmap_file_path,
                                                       ArrayCount(heightmap_file_path));

    S32 tex_width, tex_height, tex_channels;
    stbi_uc* pixels = stbi_load((char*)heightmap_path_abs.str, &tex_width, &tex_height,
                                &tex_channels, STBI_rgb_alpha);
    VkDeviceSize image_size = tex_width * tex_height * 4;
    terrain->vk_mip_levels = (U32)(floor(log2(Max(tex_width, tex_height)))) + 1;

    if (!pixels)
    {
        exitWithError("failed to load texture image!");
    }

    VK_BufferCreate(vk_ctx->physical_device, vk_ctx->device, image_size,
                    VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    &vk_texture_staging_buffer, &vk_texture_staging_buffer_memory);

    void* data;
    vkMapMemory(vk_ctx->device, vk_texture_staging_buffer_memory, 0, image_size, 0, &data);
    memcpy(data, pixels, image_size);
    vkUnmapMemory(vk_ctx->device, vk_texture_staging_buffer_memory);

    stbi_image_free(pixels);

    VK_ImageCreate(vk_ctx->physical_device, vk_ctx->device, tex_width, tex_height,
                   VK_SAMPLE_COUNT_1_BIT, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL,
                   VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                       VK_IMAGE_USAGE_SAMPLED_BIT,
                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &terrain->vk_texture_image,
                   &terrain->vk_texture_image_memory, terrain->vk_mip_levels);

    VkCommandBuffer command_buffer = VK_BeginSingleTimeCommands(vk_ctx);
    VK_ImageLayoutTransition(command_buffer, terrain->vk_texture_image, VK_FORMAT_R8G8B8A8_SRGB,
                             VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             terrain->vk_mip_levels);
    VK_ImageFromBufferCopy(command_buffer, vk_texture_staging_buffer, terrain->vk_texture_image,
                           tex_width, tex_height);
    VK_GenerateMipmaps(
        command_buffer, terrain->vk_texture_image, tex_width, tex_height,
        terrain->vk_mip_levels); // TODO: mip maps are usually not generated at runtime. They are
                                 // usually stored in the texture file
    VK_EndSingleTimeCommands(vk_ctx, command_buffer);

    vkDestroyBuffer(vk_ctx->device, vk_texture_staging_buffer, nullptr);
    vkFreeMemory(vk_ctx->device, vk_texture_staging_buffer_memory, nullptr);

    VK_ImageViewCreate(&terrain->vk_texture_image_view, vk_ctx->device, terrain->vk_texture_image,
                       VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT, terrain->vk_mip_levels);
    VK_SamplerCreate(&terrain->vk_texture_sampler, vk_ctx->device, VK_FILTER_LINEAR,
                     VK_SAMPLER_MIPMAP_MODE_LINEAR, terrain->vk_mip_levels,
                     vk_ctx->physical_device_properties.limits.maxSamplerAnisotropy);

    scratch_end(scratch);
}

internal void
TerrainDescriptorSetCreate(Terrain* terrain, U32 frames_in_flight)
{
    Temp scratch = scratch_begin(0, 0);
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
        buffer_info.range = sizeof(TerrainUniformBuffer);

        VkWriteDescriptorSet uniform_buffer_desc{};
        uniform_buffer_desc.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        uniform_buffer_desc.dstSet = terrain->descriptor_sets[i];
        uniform_buffer_desc.dstBinding = 0;
        uniform_buffer_desc.dstArrayElement = 0;
        uniform_buffer_desc.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uniform_buffer_desc.descriptorCount = 1;
        uniform_buffer_desc.pBufferInfo = &buffer_info;
        uniform_buffer_desc.pImageInfo = nullptr;
        uniform_buffer_desc.pTexelBufferView = nullptr;

        VkDescriptorImageInfo image_info{};
        image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        image_info.imageView = terrain->vk_texture_image_view;
        image_info.sampler = terrain->vk_texture_sampler;

        VkWriteDescriptorSet texture_sampler_desc{};
        texture_sampler_desc.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        texture_sampler_desc.dstSet = terrain->descriptor_sets[i];
        texture_sampler_desc.dstBinding = 1;
        texture_sampler_desc.dstArrayElement = 0;
        texture_sampler_desc.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        texture_sampler_desc.descriptorCount = 1;
        texture_sampler_desc.pImageInfo = &image_info;

        VkWriteDescriptorSet descriptors[] = {uniform_buffer_desc, texture_sampler_desc};

        vkUpdateDescriptorSets(vk_ctx->device, ArrayCount(descriptors), descriptors, 0, nullptr);
    }

    scratch_end(scratch);
}

internal void
TerrainUniformBufferCreate(Terrain* terrain, U32 image_count)
{
    VulkanContext* vk_ctx = GlobalContextGet()->vulkanContext;
    VkDeviceSize terrain_buffer_size = sizeof(TerrainUniformBuffer);

    for (U32 i = 0; i < image_count; i++)
    {
        VK_BufferCreate(vk_ctx->physical_device, vk_ctx->device, terrain_buffer_size,
                        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                        &terrain->buffer[i], &terrain->buffer_memory[i]);

        if (vkMapMemory(vk_ctx->device, terrain->buffer_memory[i], 0, terrain_buffer_size, 0,
                        &terrain->buffer_memory_mapped[i]) != VK_SUCCESS)
        {
            exitWithError("failed to map terrain buffer memory!");
        }
    }
}

internal void
TerrainGraphicsPipelineCreate(Terrain* terrain, const char* cwd)
{
    Temp scratch = scratch_begin(0, 0);

    VulkanContext* vk_ctx = GlobalContextGet()->vulkanContext;

    const char* vertex_path_strs[] = {cwd, "shaders", "terrain_vert.spv"};
    String8 vertex_path_abs = CreatePathFromStrings(scratch.arena, (char**)vertex_path_strs,
                                                    ArrayCount(vertex_path_strs));

    const char* fragment_path_strings[] = {cwd, "shaders", "terrain_frag.spv"};
    String8 fragment_path_abs = CreatePathFromStrings(scratch.arena, (char**)fragment_path_strings,
                                                      ArrayCount(fragment_path_strings));

    const char* tesc_path_strings[] = {cwd, "shaders", "terrain_tesc.spv"};
    String8 tesc_path_abs = CreatePathFromStrings(scratch.arena, (char**)tesc_path_strings,
                                                  ArrayCount(tesc_path_strings));

    const char* tese_path_strings[] = {cwd, "shaders", "terrain_tese.spv"};
    String8 tese_path_abs = CreatePathFromStrings(scratch.arena, (char**)tese_path_strings,
                                                  ArrayCount(tese_path_strings));

    Buffer<U8> vert_shader_buffer = IO_ReadFile(scratch.arena, vertex_path_abs);
    Buffer<U8> frag_shader_buffer = IO_ReadFile(scratch.arena, fragment_path_abs);
    Buffer<U8> tesc_shader_buffer = IO_ReadFile(scratch.arena, tesc_path_abs);
    Buffer<U8> tese_shader_buffer = IO_ReadFile(scratch.arena, tese_path_abs);

    VkShaderModule vert_shader_module = VK_ShaderModuleCreate(vk_ctx->device, vert_shader_buffer);
    VkShaderModule frag_shader_module = VK_ShaderModuleCreate(vk_ctx->device, frag_shader_buffer);
    VkShaderModule tesc_shader_module = VK_ShaderModuleCreate(vk_ctx->device, tesc_shader_buffer);
    VkShaderModule tese_shader_module = VK_ShaderModuleCreate(vk_ctx->device, tese_shader_buffer);

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

    VkPipelineShaderStageCreateInfo tesc_shader_stage_info{};
    tesc_shader_stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    tesc_shader_stage_info.stage = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
    tesc_shader_stage_info.module = tesc_shader_module;
    tesc_shader_stage_info.pName = "main";

    VkPipelineShaderStageCreateInfo tese_shader_stage_info{};
    tese_shader_stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    tese_shader_stage_info.stage = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
    tese_shader_stage_info.module = tese_shader_module;
    tese_shader_stage_info.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {
        vert_shader_stage_info, frag_shader_stage_info, tesc_shader_stage_info,
        tese_shader_stage_info};

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
    viewport.width = vk_ctx->swapchain_extent.width;
    viewport.height = vk_ctx->swapchain_extent.height;
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
    rasterizer.polygonMode = VK_POLYGON_MODE_LINE; // TODO: helps in debugging, change to fill later
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE; // TODO: might need to use counter-clockwise
    rasterizer.lineWidth = 1.0f;
    rasterizer.depthBiasEnable = VK_FALSE;
    rasterizer.depthBiasConstantFactor = 0.0f; // Optional
    rasterizer.depthBiasClamp = 0.0f;          // Optional
    rasterizer.depthBiasSlopeFactor = 0.0f;    // Optional

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_TRUE;
    multisampling.rasterizationSamples =
        vk_ctx->msaa_samples;                       // TODO: might only be necessary with one
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

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp =
        VK_COMPARE_OP_LESS; // TODO: might need to change to VK_COMPARE_OP_GREATER
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.minDepthBounds = 0.0f; // Optional
    depthStencil.maxDepthBounds = 1.0f; // Optional
    depthStencil.stencilTestEnable = VK_FALSE;
    depthStencil.front = {}; // Optional
    depthStencil.back = {};  // Optional

    VkPipelineTessellationStateCreateInfo pipeline_tessellation_state_create_info{};
    pipeline_tessellation_state_create_info.sType =
        VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO;
    pipeline_tessellation_state_create_info.patchControlPoints = 4;

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
    pipelineInfo.pDepthStencilState = &depthStencil; // Optional
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.pTessellationState = &pipeline_tessellation_state_create_info;
    pipelineInfo.layout = terrain->vk_pipeline_layout;
    pipelineInfo.renderPass = vk_ctx->vk_renderpass;
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

    vkDestroySampler(vk_ctx->device, terrain->vk_texture_sampler, nullptr);
    vkDestroyImageView(vk_ctx->device, terrain->vk_texture_image_view, nullptr);
    vkDestroyImage(vk_ctx->device, terrain->vk_texture_image, nullptr);
    vkFreeMemory(vk_ctx->device, terrain->vk_texture_image_memory, nullptr);

    vkDestroyDescriptorPool(vk_ctx->device, terrain->descriptor_pool, nullptr);

    vkDestroyDescriptorSetLayout(vk_ctx->device, terrain->descriptor_set_layout, nullptr);
    vkDestroyPipelineLayout(vk_ctx->device, terrain->vk_pipeline_layout, nullptr);
}

internal void
UpdateTerrainUniformBuffer(Terrain* terrain, Vec2F32 screen_res, U32 current_frame)
{
    static U64 start_time = os_now_microseconds();
    U64 current_time = os_now_microseconds();
    U64 elapsed_time = current_time - start_time;
    F32 elapsed_time_sec = (F32)elapsed_time / 1'000'000.0;
    TerrainUniformBuffer* ubo = &terrain->uniform_buffer;

    ubo->model =
        glm::rotate(glm::mat4(1.0f), (1 * glm::radians(90.0f)), glm::vec3(0.0f, 0.0f, 1.0f));
    ubo->view = glm::lookAt(glm::vec3(10.0f, 10.0f, -10.0f), glm::vec3(0.0f, 0.0f, 0.0f),
                            glm::vec3(0.0f, 1.0f, 0.0f));
    ubo->proj = glm::perspective(glm::radians(45.0f), (screen_res.x / screen_res.y), 0.1f, 100.0f);
    ubo->proj[1][1] *= -1;

    glm::mat4 transform = ubo->proj * ubo->view;
    FrustumPlanesCalculate(&ubo->frustum, transform);
    ubo->viewport_dim = screen_res;
    ubo->displacement_factor = 1.0f;
    ubo->tessellated_edge_size = 1.0f;
    ubo->tessellation_factor = 1.0f;

    MemoryCopy(terrain->buffer_memory_mapped[current_frame], ubo, sizeof(*ubo));
}

internal void
TerrainRenderPassBegin(VulkanContext* vk_ctx, Terrain* terrain, U32 image_index, U32 current_frame)
{
    VkExtent2D swap_chain_extent = vk_ctx->swapchain_extent;
    VkCommandBuffer command_buffer = vk_ctx->command_buffers.data[current_frame];

    VkRenderPassBeginInfo renderpass_info{};
    renderpass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderpass_info.renderPass = vk_ctx->vk_renderpass;
    renderpass_info.framebuffer = vk_ctx->swapchain_framebuffers.data[image_index];
    renderpass_info.renderArea.offset = {0, 0};
    renderpass_info.renderArea.extent = swap_chain_extent;

    const U32 clear_value_count = 3;
    VkClearValue clear_values[clear_value_count] = {0};
    clear_values[0].color = {{0.0f, 0.0f, 0.0f, 0.0f}};
    clear_values[1].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    clear_values[2].depthStencil = {1.0f, 0};

    renderpass_info.clearValueCount = clear_value_count;
    renderpass_info.pClearValues = &clear_values[0];

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

    vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            terrain->vk_pipeline_layout, 0, 1,
                            &terrain->descriptor_sets[current_frame], 0, nullptr);

    VkBuffer vertex_buffers[] = {vk_ctx->vk_vertex_context.buffer};
    VkDeviceSize offsets[] = {0};

    vkCmdBindVertexBuffers(command_buffer, 0, 1, vertex_buffers, offsets);

    vkCmdBindIndexBuffer(command_buffer, vk_ctx->vk_indice_context.buffer, 0, VK_INDEX_TYPE_UINT32);

    vkCmdDrawIndexed(command_buffer, vk_ctx->vk_indice_context.size, 1, 0, 0, 0);

    vkCmdEndRenderPass(command_buffer);
}

internal void
TerrainGenerateBuffers(Arena* arena, Buffer<Vertex>* vertices, Buffer<U32>* indices, U32 patch_size)
{
    const F32 uv_scale = 1.0f;
    const U32 vertex_count = patch_size * patch_size;
    *vertices = BufferAlloc<Vertex>(arena, vertex_count);

    const F32 wx = 2.0f;
    const F32 wy = 2.0f;

    for (U32 x = 0; x < patch_size; x++)
    {
        for (U32 y = 0; y < patch_size; y++)
        {
            U32 index = (x + y * patch_size);
            vertices->data[index].pos.x = x * wx + wx / 2.0f - (F32)patch_size * wx / 2.0f;
            vertices->data[index].pos.y = 0.0f;
            vertices->data[index].pos.z = y * wy + wy / 2.0f - (F32)patch_size * wy / 2.0f;
            vertices->data[index].uv = {(F32)x / patch_size * uv_scale,
                                        (F32)y / patch_size * uv_scale};
        }
    }

    // Indices
    const U32 w = (patch_size - 1);
    const U32 index_count = w * w * 4;
    *indices = BufferAlloc<U32>(arena, index_count);
    for (U32 x = 0; x < w; x++)
    {
        for (auto y = 0; y < w; y++)
        {
            U32 index = (x + y * w) * 4;
            indices->data[index] = (x + y * patch_size);
            indices->data[index + 1] = indices->data[index] + patch_size;
            indices->data[index + 2] = indices->data[index + 1] + 1;
            indices->data[index + 3] = indices->data[index] + 1;
        }
    }
}

internal void
TerrainInit()
{
    Context* ctx = GlobalContextGet();
    VulkanContext* vk_ctx = ctx->vulkanContext;

    ctx->terrain = push_struct(ctx->arena_permanent, Terrain);
    ctx->terrain->patch_size = 16;
    TerrainAllocations(vk_ctx->arena, ctx->terrain, vk_ctx->MAX_FRAMES_IN_FLIGHT);
    TerrainDescriptorPoolCreate(ctx->terrain, vk_ctx->MAX_FRAMES_IN_FLIGHT);
    TerrainDescriptorSetLayoutCreate(vk_ctx->device, ctx->terrain);
    TerrainUniformBufferCreate(ctx->terrain, vk_ctx->MAX_FRAMES_IN_FLIGHT);
    TerrainDescriptorSetCreate(ctx->terrain, vk_ctx->MAX_FRAMES_IN_FLIGHT);
    TerrainGraphicsPipelineCreate(ctx->terrain, ctx->cwd);

    TerrainTextureResourceCreate(vk_ctx, ctx->terrain, ctx->cwd);

    // TODO: move function below
    TerrainGenerateBuffers(vk_ctx->arena, &ctx->terrain->vertices, &ctx->terrain->indices,
                           ctx->terrain->patch_size);
}

internal VkVertexInputBindingDescription
TerrainBindingDescriptionGet()
{
    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(Vertex);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    return bindingDescription;
}

internal Buffer<VkVertexInputAttributeDescription>
TerrainAttributeDescriptionGet(Arena* arena)
{
    Buffer<VkVertexInputAttributeDescription> attribute_descriptions =
        BufferAlloc<VkVertexInputAttributeDescription>(arena, 2);
    attribute_descriptions.data[0].binding = 0;
    attribute_descriptions.data[0].location = 0;
    attribute_descriptions.data[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attribute_descriptions.data[0].offset = offsetof(Vertex, pos);

    attribute_descriptions.data[1].binding = 0;
    attribute_descriptions.data[1].location = 1;
    attribute_descriptions.data[1].format = VK_FORMAT_R32G32_SFLOAT;
    attribute_descriptions.data[1].offset = offsetof(Vertex, uv);

    return attribute_descriptions;
}

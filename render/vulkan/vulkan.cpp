
// ~mgj: global context
static VK_Context* g_vk_ctx = 0;

// ~mgj: vulkan context

static void
VK_CtxSet(VK_Context* vk_ctx)
{
    Assert(!g_vk_ctx);
    g_vk_ctx = vk_ctx;
}

static VK_Context*
VK_CtxGet()
{
    Assert(g_vk_ctx);
    return g_vk_ctx;
}
static void
VK_TextureDestroy(VK_Context* vk_ctx, VK_Texture* texture)
{
    VK_ImageResourceDestroy(vk_ctx->allocator, texture->image_resource);
    vkDestroySampler(vk_ctx->device, texture->sampler, nullptr);
}
static void
VK_ImageFromKtx2file(VkCommandBuffer cmd, VkImage image, VK_BufferAllocation staging_buffer,
                     VK_Context* vk_ctx, ktxTexture2* ktx_texture)
{
    ScratchScope scratch = ScratchScope(0, 0);

    VkDeviceSize img_size = ktx_texture->dataSize;
    U32 mip_levels = ktx_texture->numLevels;

    vmaCopyMemoryToAllocation(vk_ctx->allocator, ktx_texture->pData, staging_buffer.allocation, 0,
                              img_size);

    VkBufferImageCopy* regions = PushArray(scratch.arena, VkBufferImageCopy, mip_levels);

    for (uint32_t level = 0; level < mip_levels; ++level)
    {
        ktx_size_t offset;
        ktxTexture_GetImageOffset((ktxTexture*)ktx_texture, level, 0, 0, &offset);

        uint32_t mip_width = ktx_texture->baseWidth >> level;
        uint32_t mip_height = ktx_texture->baseHeight >> level;
        uint32_t mip_depth = ktx_texture->baseDepth >> level;

        if (mip_width == 0)
            mip_width = 1;
        if (mip_height == 0)
            mip_height = 1;
        if (mip_depth == 0)
            mip_depth = 1;

        regions[level] = {.bufferOffset = offset,
                          .bufferRowLength = 0, // tightly packed
                          .bufferImageHeight = 0,
                          .imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                               .mipLevel = level,
                                               .baseArrayLayer = 0,
                                               .layerCount = 1},
                          .imageOffset = {0, 0, 0},
                          .imageExtent = {mip_width, mip_height, mip_depth}};
    }

    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                             .baseMipLevel = 0,
                             .levelCount = mip_levels,
                             .baseArrayLayer = 0,
                             .layerCount = 1},
    };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                         0, NULL, 0, NULL, 1, &barrier);
    vkCmdCopyBufferToImage(cmd, staging_buffer.buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           mip_levels, regions);

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, NULL, 0, NULL, 1, &barrier);
}

static void
VK_AssetBufferLoad(R_AssetInfo* asset_info, R_BufferInfo* buffer_info)
{
    VK_Context* vk_ctx = VK_CtxGet();
    VK_AssetManager* asset_manager = vk_ctx->asset_manager;
    R_AssetItem<VK_AssetItemBuffer>* asset_item = VK_AssetManagerBufferItemGet(asset_info->id);

    // ~mgj: Create buffer allocation
    VmaAllocationCreateInfo vma_info = {0};
    vma_info.usage = VMA_MEMORY_USAGE_AUTO;
    vma_info.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    VkBufferUsageFlags usage_flags = NULL;
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
    VK_AssetItemBuffer* asset_buffer = &asset_item->item;
    asset_buffer->buffer_alloc = buffer;

    // ~mgj: Preparing buffer loading for another thread
    R_ThreadInput* thread_input = VK_ThreadInputCreate();
    R_AssetLoadingInfo* asset_load_info = &thread_input->asset_info;
    asset_load_info->info = *asset_info;
    R_BufferInfo* buffer_load_info = &asset_load_info->extra_info.buffer_info;
    *buffer_load_info = *buffer_info;
    asset_item->is_loading = TRUE;

    async::QueueItem queue_input = {.data = thread_input, .worker_func = VK_ThreadSetup};
    async::QueuePush(asset_manager->work_queue, &queue_input);
}

static void
VK_AssetTextureLoad(R_AssetInfo* asset_info, R_SamplerInfo* sampler_info, String8 texture_path,
                    R_PipelineUsageType pipeline_usage_type)
{
    VK_Context* vk_ctx = VK_CtxGet();
    VK_AssetManager* asset_manager = vk_ctx->asset_manager;

    // ~mgj: Create sampler
    VkSamplerCreateInfo sampler_create_info = {};
    VK_SamplerCreateInfoFromSamplerInfo(sampler_info, &sampler_create_info);
    sampler_create_info.anisotropyEnable = VK_TRUE;
    sampler_create_info.maxAnisotropy = (F32)vk_ctx->msaa_samples;
    VkSampler vk_sampler = VK_SamplerCreate(vk_ctx->device, &sampler_create_info);

    // Get texture dimensions
    ktxTexture2* ktx_texture;
    ktx_error_code_e ktxresult = ktxTexture2_CreateFromNamedFile(
        (char*)texture_path.str, KTX_TEXTURE_CREATE_NO_STORAGE, &ktx_texture);
    Assert(ktxresult == KTX_SUCCESS);

    // ~mgj: Create Image and Image View
    VmaAllocationCreateInfo vma_info = {.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
    VK_ImageAllocation image_alloc = VK_ImageAllocationCreate(
        vk_ctx->allocator, ktx_texture->baseWidth, ktx_texture->baseHeight, VK_SAMPLE_COUNT_1_BIT,
        VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
            VK_IMAGE_USAGE_SAMPLED_BIT,
        ktx_texture->numLevels, vma_info);

    VK_ImageViewResource image_view_resource =
        VK_ImageViewResourceCreate(vk_ctx->device, image_alloc.image, VK_FORMAT_R8G8B8A8_SRGB,
                                   VK_IMAGE_ASPECT_COLOR_BIT, ktx_texture->numLevels);

    // ~mgj: Choose Descriptor Set Layout and Create Descriptor Set
    VkDescriptorSetLayout desc_set_layout = NULL;
    switch (pipeline_usage_type)
    {
        case R_PipelineUsageType_3D:
            desc_set_layout = vk_ctx->model_3D_pipeline.descriptor_set_layout;
            break;
        case R_PipelineUsageType_3DInstanced:
            desc_set_layout = vk_ctx->model_3D_pipeline.descriptor_set_layout;
            break;
        default: InvalidPath;
    }
    VkDescriptorSet texture_handle =
        VK_DescriptorSetCreate(vk_ctx->arena, vk_ctx->device, vk_ctx->descriptor_pool,
                               desc_set_layout, image_view_resource.image_view, vk_sampler);

    // ~mgj: Assign values to texture
    R_AssetItem<VK_Texture>* asset_item = VK_AssetManagerTextureItemGet(asset_info->id);
    VK_Texture* texture = &asset_item->item;
    texture->image_resource = {.image_alloc = image_alloc,
                               .image_view_resource = image_view_resource};
    texture->desc_set = texture_handle;
    texture->pipeline_usage_type = pipeline_usage_type;
    texture->sampler = vk_sampler;

    // ~mgj: make input ready for texture loading on thread
    R_ThreadInput* thread_input = VK_ThreadInputCreate();
    R_AssetLoadingInfo* asset_load_info = &thread_input->asset_info;
    asset_load_info->info = *asset_info;
    R_TextureLoadingInfo* texture_load_info = &asset_load_info->extra_info.texture_info;
    texture_load_info->texture_path = PushStr8Copy(thread_input->arena, texture_path);
    asset_item->is_loading = TRUE;

    async::QueueItem queue_input = {.data = thread_input, .worker_func = VK_ThreadSetup};
    async::QueuePush(asset_manager->work_queue, &queue_input);
}

static R_ThreadInput*
VK_ThreadInputCreate()
{
    Arena* arena = ArenaAlloc();
    R_ThreadInput* thread_input = PushStruct(arena, R_ThreadInput);
    thread_input->arena = arena;
    return thread_input;
}

static void
VK_ThreadInputDestroy(R_ThreadInput* thread_input)
{
    ArenaRelease(thread_input->arena);
}

static void
VK_ThreadSetup(async::ThreadInfo thread_info, void* input)
{
    R_ThreadInput* thread_input = (R_ThreadInput*)input;

    VK_Context* vk_ctx = VK_CtxGet();
    VK_AssetManager* asset_store = vk_ctx->asset_manager;

    // ~mgj: Record the command buffer
    VkCommandBuffer cmd = VK_BeginCommand(
        vk_ctx->device, asset_store->threaded_cmd_pools.data[thread_info.thread_id]); // Your helper

    R_AssetLoadingInfo* asset_loading_info = &thread_input->asset_info;
    Assert(asset_loading_info->info.id.id != 0);

    switch (asset_loading_info->info.type)
    {
        case R_AssetItemType_Texture:
        {
            R_TextureLoadingInfo* extra_info =
                (R_TextureLoadingInfo*)&asset_loading_info->extra_info;
            VK_TextureCreate(cmd, asset_loading_info->info, extra_info->texture_path);
        }
        break;
        case R_AssetItemType_Buffer:
        {
            R_BufferInfo* buffer_info = (R_BufferInfo*)&asset_loading_info->extra_info;
            VK_AssetInfoBufferCmd(cmd, asset_loading_info->info.id, buffer_info->buffer);
        }
        break;
        default: InvalidPath;
    }
    VK_CHECK_RESULT(vkEndCommandBuffer(cmd));

    // ~mgj: Enqueue the command buffer
    VK_AssetCmdQueueItemEnqueue(thread_info.thread_id, cmd, thread_input);
}

static void
VK_TextureCreate(VkCommandBuffer cmd_buffer, R_AssetInfo asset_info, String8 texture_path)
{
    VK_Context* vk_ctx = VK_CtxGet();
    R_AssetItem<VK_Texture>* asset_store_texture = VK_AssetManagerTextureItemGet(asset_info.id);
    VK_Texture* texture = &asset_store_texture->item;

    // Get texture
    ktxTexture2* ktx_texture;
    ktx_error_code_e ktxresult = ktxTexture2_CreateFromNamedFile(
        (char*)texture_path.str, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &ktx_texture);
    Assert(ktxresult == KTX_SUCCESS);

    VK_BufferAllocation texture_staging_buffer =
        VK_StagingBufferCreate(vk_ctx->allocator, ktx_texture->dataSize);

    VK_ImageFromKtx2file(cmd_buffer, texture->image_resource.image_alloc.image,
                         texture_staging_buffer, vk_ctx, ktx_texture);

    texture->staging_buffer = texture_staging_buffer;
}

static VK_Pipeline
VK_Model3DInstancePipelineCreate(VK_Context* vk_ctx, String8 shader_path)
{
    ScratchScope scratch = ScratchScope(0, 0);

    VkDescriptorSetLayoutBinding desc_set_layout_info = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
    };
    VkDescriptorSetLayout desc_set_layout =
        VK_DescriptorSetLayoutCreate(vk_ctx->device, &desc_set_layout_info, 1);

    String8 vert_path = CreatePathFromStrings(
        scratch.arena,
        Str8BufferFromCString(scratch.arena, {(char*)shader_path.str, "model_3d_instancing",
                                              "model_3d_instancing_vert.spv"}));
    String8 frag_path = CreatePathFromStrings(
        scratch.arena,
        Str8BufferFromCString(scratch.arena, {(char*)shader_path.str, "model_3d_instancing",
                                              "model_3d_instancing_frag.spv"}));

    VK_ShaderModuleInfo vert_shader_stage_info = VK_ShaderStageFromSpirv(
        scratch.arena, vk_ctx->device, VK_SHADER_STAGE_VERTEX_BIT, vert_path);
    VK_ShaderModuleInfo frag_shader_stage_info = VK_ShaderStageFromSpirv(
        scratch.arena, vk_ctx->device, VK_SHADER_STAGE_FRAGMENT_BIT, frag_path);

    VkPipelineShaderStageCreateInfo shader_stages[] = {vert_shader_stage_info.info,
                                                       frag_shader_stage_info.info};

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

    VkPipelineDynamicStateCreateInfo dynamic_state{};
    dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount = (U32)(ArrayCount(dynamicStates));
    dynamic_state.pDynamicStates = dynamicStates;

    VkPipelineVertexInputStateCreateInfo vertex_input_info{};
    vertex_input_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkVertexInputAttributeDescription attr_desc[] = {
        {.location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT},
        {.location = 1,
         .binding = 0,
         .format = VK_FORMAT_R32G32_SFLOAT,
         .offset = offsetof(city::Vertex3D, uv)},
        {.location = 2,
         .binding = 1,
         .format = VK_FORMAT_R32G32B32A32_SFLOAT,
         .offset = (U32)offsetof(city::Model3DInstance, x_basis)},
        {.location = 3,
         .binding = 1,
         .format = VK_FORMAT_R32G32B32A32_SFLOAT,
         .offset = (U32)offsetof(city::Model3DInstance, y_basis)},
        {.location = 4,
         .binding = 1,
         .format = VK_FORMAT_R32G32B32A32_SFLOAT,
         .offset = (U32)offsetof(city::Model3DInstance, z_basis)},
        {.location = 5,
         .binding = 1,
         .format = VK_FORMAT_R32G32B32A32_SFLOAT,
         .offset = (U32)offsetof(city::Model3DInstance, w_basis)},
    };
    VkVertexInputBindingDescription input_desc[] = {
        {.binding = 0, .stride = sizeof(city::Vertex3D), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX},
        {.binding = 1,
         .stride = sizeof(city::Model3DInstance),
         .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE}};

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

    VkPipelineColorBlendAttachmentState color_blend_attachment{
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT};

    VkPipelineColorBlendAttachmentState color_blend_attachments[] = {color_blend_attachment,
                                                                     color_blend_attachment};
    VkPipelineColorBlendStateCreateInfo color_blending{};
    color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blending.attachmentCount = ArrayCount(color_blend_attachments);
    color_blending.pAttachments = color_blend_attachments;

    VkDescriptorSetLayout descriptor_set_layouts[] = {vk_ctx->camera_descriptor_set_layout,
                                                      desc_set_layout};
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = ArrayCount(descriptor_set_layouts);
    pipelineLayoutInfo.pSetLayouts = descriptor_set_layouts;

    VkPipelineDepthStencilStateCreateInfo depth_stencil{};
    depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil.depthTestEnable = VK_TRUE;
    depth_stencil.depthWriteEnable = VK_TRUE;
    depth_stencil.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineLayout pipeline_layout;
    if (vkCreatePipelineLayout(vk_ctx->device, &pipelineLayoutInfo, nullptr, &pipeline_layout) !=
        VK_SUCCESS)
    {
        exitWithError("failed to create pipeline layout!");
    }

    VkFormat color_attachment_formats[] = {vk_ctx->swapchain_resources->color_format,
                                           vk_ctx->swapchain_resources->object_id_image_format};

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
    pipeline_create_info.pDepthStencilState = &depth_stencil; // Optional
    pipeline_create_info.pColorBlendState = &color_blending;
    pipeline_create_info.pDynamicState = &dynamic_state;
    pipeline_create_info.layout = pipeline_layout;

    VkPipeline pipeline;
    if (vkCreateGraphicsPipelines(vk_ctx->device, VK_NULL_HANDLE, 1, &pipeline_create_info, nullptr,
                                  &pipeline) != VK_SUCCESS)
    {
        exitWithError("failed to create graphics pipeline!");
    }

    VK_Pipeline pipeline_info = {.pipeline = pipeline,
                                 .pipeline_layout = pipeline_layout,
                                 .descriptor_set_layout = desc_set_layout};
    return pipeline_info;
}

static VK_Pipeline
VK_Model3DPipelineCreate(VK_Context* vk_ctx, String8 shader_path)
{
    ScratchScope scratch = ScratchScope(0, 0);

    VkDescriptorSetLayoutBinding desc_set_layout_info = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
    };

    VkDescriptorSetLayout desc_set_layout =
        VK_DescriptorSetLayoutCreate(vk_ctx->device, &desc_set_layout_info, 1);

    String8 vert_path = CreatePathFromStrings(
        scratch.arena, Str8BufferFromCString(scratch.arena, {(char*)shader_path.str, "model_3d",
                                                             "model_3d_vert.spv"}));
    String8 frag_path = CreatePathFromStrings(
        scratch.arena, Str8BufferFromCString(scratch.arena, {(char*)shader_path.str, "model_3d",
                                                             "model_3d_frag.spv"}));

    VK_ShaderModuleInfo vert_shader_stage_info = VK_ShaderStageFromSpirv(
        scratch.arena, vk_ctx->device, VK_SHADER_STAGE_VERTEX_BIT, vert_path);
    VK_ShaderModuleInfo frag_shader_stage_info = VK_ShaderStageFromSpirv(
        scratch.arena, vk_ctx->device, VK_SHADER_STAGE_FRAGMENT_BIT, frag_path);

    VkPipelineShaderStageCreateInfo shader_stages[] = {vert_shader_stage_info.info,
                                                       frag_shader_stage_info.info};

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
                                      VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE,
                                      VK_DYNAMIC_STATE_COLOR_WRITE_ENABLE_EXT};

    VkPipelineDynamicStateCreateInfo dynamic_state{};
    dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount = (U32)(ArrayCount(dynamicStates));
    dynamic_state.pDynamicStates = dynamicStates;

    VkPipelineVertexInputStateCreateInfo vertex_input_info{};
    vertex_input_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkVertexInputAttributeDescription attr_desc[] = {
        {.location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT},
        {.location = 1,
         .binding = 0,
         .format = VK_FORMAT_R32G32_SFLOAT,
         .offset = offsetof(city::Vertex3D, uv)},
        {.location = 2,
         .binding = 0,
         .format = vk_ctx->object_id_format,
         .offset = offsetof(city::Vertex3D, object_id)}};
    VkVertexInputBindingDescription input_desc[] = {
        {.binding = 0, .stride = sizeof(city::Vertex3D), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX}};

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

    VkPipelineColorBlendAttachmentState color_blend_attachment{
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT};

    VkPipelineColorBlendAttachmentState color_blend_attachments[] = {color_blend_attachment,
                                                                     color_blend_attachment};
    VkPipelineColorBlendStateCreateInfo color_blending{};
    color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blending.attachmentCount = ArrayCount(color_blend_attachments);
    color_blending.pAttachments = color_blend_attachments;

    VkDescriptorSetLayout descriptor_set_layouts[] = {vk_ctx->camera_descriptor_set_layout,
                                                      desc_set_layout};
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = ArrayCount(descriptor_set_layouts);
    pipelineLayoutInfo.pSetLayouts = descriptor_set_layouts;

    VkPipelineDepthStencilStateCreateInfo depth_stencil{};
    depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil.depthTestEnable = VK_TRUE;
    depth_stencil.depthWriteEnable = VK_TRUE;
    depth_stencil.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineLayout pipeline_layout;
    if (vkCreatePipelineLayout(vk_ctx->device, &pipelineLayoutInfo, nullptr, &pipeline_layout) !=
        VK_SUCCESS)
    {
        exitWithError("failed to create pipeline layout!");
    }

    VkFormat color_attachment_formats[] = {vk_ctx->swapchain_resources->color_format,
                                           vk_ctx->swapchain_resources->object_id_image_format};
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
    pipeline_create_info.pDepthStencilState = &depth_stencil; // Optional
    pipeline_create_info.pColorBlendState = &color_blending;
    pipeline_create_info.pDynamicState = &dynamic_state;
    pipeline_create_info.layout = pipeline_layout;

    VkPipeline pipeline;
    if (vkCreateGraphicsPipelines(vk_ctx->device, VK_NULL_HANDLE, 1, &pipeline_create_info, nullptr,
                                  &pipeline) != VK_SUCCESS)
    {
        exitWithError("failed to create graphics pipeline!");
    }

    VK_Pipeline pipeline_info = {.pipeline = pipeline,
                                 .pipeline_layout = pipeline_layout,
                                 .descriptor_set_layout = desc_set_layout};
    return pipeline_info;
}

static void
VK_Model3DInstanceRendering()
{
    VK_Context* vk_ctx = VK_CtxGet();
    VK_SwapchainResources* swapchain_resources = vk_ctx->swapchain_resources;
    VkExtent2D swapchain_extent = swapchain_resources->swapchain_extent;
    VkCommandBuffer cmd_buffer = vk_ctx->command_buffers.data[vk_ctx->current_frame];
    VK_Pipeline* pipeline = &vk_ctx->model_3D_instance_pipeline;

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
    VK_BufferAllocation* instance_buffer_alloc = &vk_ctx->model_3D_instance_buffer;
    VK_Model3DInstance* model_3D_instance_draw = &vk_ctx->draw_frame->model_3D_instance_draw;

    VkDescriptorSet descriptor_sets[2] = {vk_ctx->camera_descriptor_sets[vk_ctx->current_frame]};
    VK_BufferAllocCreateOrResize(vk_ctx->allocator,
                                 model_3D_instance_draw->total_instance_buffer_byte_count,
                                 instance_buffer_alloc, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

    for (VK_Model3DInstanceNode* node = vk_ctx->draw_frame->model_3D_instance_draw.list.first; node;
         node = node->next)
    {
        vmaCopyMemoryToAllocation(vk_ctx->allocator, node->instance_buffer_info.buffer.data,
                                  instance_buffer_alloc->allocation, node->instance_buffer_offset,
                                  node->instance_buffer_info.buffer.size);
        VkBuffer vertex_buffers[] = {
            node->vertex_alloc.buffer,
            instance_buffer_alloc->buffer,
        };
        VkDeviceSize vertex_offsets[] = {0, node->instance_buffer_offset};
        descriptor_sets[1] = {node->texture_handle};
        vkCmdBindDescriptorSets(cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipeline->pipeline_layout, 0, ArrayCount(descriptor_sets),
                                descriptor_sets, 0, NULL);
        vkCmdBindVertexBuffers(cmd_buffer, 0, 2, vertex_buffers, vertex_offsets);
        U32 instance_count =
            U32(node->instance_buffer_info.buffer.size / node->instance_buffer_info.type_size);
        U32 index_count = U32(node->index_alloc.size / sizeof(U32));
        vkCmdBindIndexBuffer(cmd_buffer, node->index_alloc.buffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd_buffer, index_count, instance_count, 0, 0, 0);
    }
}

// ~mgj: Camera functions

static void
VK_CameraUniformBufferCreate(VK_Context* vk_ctx)
{
    VkDeviceSize camera_buffer_size = sizeof(VK_CameraUniformBuffer);
    VkBufferUsageFlags buffer_usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

    for (U32 i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; i++)
    {
        vk_ctx->camera_buffer_alloc_mapped[i] =
            VK_BufferMappedCreate(vk_ctx->allocator, camera_buffer_size, buffer_usage);
    }
}

static void
VK_CameraUniformBufferUpdate(VK_Context* vk_ctx, ui::Camera* camera, Vec2F32 screen_res,
                             U32 current_frame)
{
    VK_BufferAllocationMapped* buffer = &vk_ctx->camera_buffer_alloc_mapped[current_frame];
    VK_CameraUniformBuffer* ubo = (VK_CameraUniformBuffer*)buffer->mapped_ptr;
    glm::mat4 transform = camera->projection_matrix * camera->view_matrix;
    VK_FrustumPlanesCalculate(&ubo->frustum, transform);
    ubo->viewport_dim.x = screen_res.x;
    ubo->viewport_dim.y = screen_res.y;
    ubo->view = camera->view_matrix;
    ubo->proj = camera->projection_matrix;

    VK_BufferMappedUpdate(vk_ctx->command_buffers.data[current_frame], vk_ctx->allocator, *buffer);
}

static void
VK_CameraDescriptorSetLayoutCreate(VK_Context* vk_ctx)
{
    VkDescriptorSetLayoutBinding camera_desc_layout{};
    camera_desc_layout.binding = 0;
    camera_desc_layout.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    camera_desc_layout.descriptorCount = 1;
    camera_desc_layout.stageFlags = VK_SHADER_STAGE_VERTEX_BIT |
                                    VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT |
                                    VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;

    VkDescriptorSetLayoutBinding descriptor_layout_bindings[] = {camera_desc_layout};

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = ArrayCount(descriptor_layout_bindings);
    layoutInfo.pBindings = descriptor_layout_bindings;

    if (vkCreateDescriptorSetLayout(vk_ctx->device, &layoutInfo, nullptr,
                                    &vk_ctx->camera_descriptor_set_layout) != VK_SUCCESS)
    {
        exitWithError("failed to create camera descriptor set layout!");
    }
}

static void
VK_CameraDescriptorSetCreate(VK_Context* vk_ctx)
{
    Temp scratch = ScratchBegin(0, 0);
    Arena* arena = scratch.arena;

    Buffer<VkDescriptorSetLayout> layouts =
        BufferAlloc<VkDescriptorSetLayout>(arena, VK_MAX_FRAMES_IN_FLIGHT);

    for (U32 i = 0; i < layouts.size; i++)
    {
        layouts.data[i] = vk_ctx->camera_descriptor_set_layout;
    }

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = vk_ctx->descriptor_pool;
    allocInfo.descriptorSetCount = layouts.size;
    allocInfo.pSetLayouts = layouts.data;

    if (vkAllocateDescriptorSets(vk_ctx->device, &allocInfo, vk_ctx->camera_descriptor_sets) !=
        VK_SUCCESS)
    {
        exitWithError("CameraDescriptorSetCreate: failed to allocate descriptor sets!");
    }

    for (size_t i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; i++)
    {
        VkDescriptorBufferInfo buffer_info{};
        buffer_info.buffer = vk_ctx->camera_buffer_alloc_mapped[i].buffer_alloc.buffer;
        buffer_info.offset = 0;
        buffer_info.range = sizeof(VK_CameraUniformBuffer);

        VkWriteDescriptorSet uniform_buffer_desc{};
        uniform_buffer_desc.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        uniform_buffer_desc.dstSet = vk_ctx->camera_descriptor_sets[i];
        uniform_buffer_desc.dstBinding = 0;
        uniform_buffer_desc.dstArrayElement = 0;
        uniform_buffer_desc.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uniform_buffer_desc.descriptorCount = 1;
        uniform_buffer_desc.pBufferInfo = &buffer_info;
        uniform_buffer_desc.pImageInfo = nullptr;
        uniform_buffer_desc.pTexelBufferView = nullptr;

        VkWriteDescriptorSet descriptors[] = {uniform_buffer_desc};

        vkUpdateDescriptorSets(vk_ctx->device, ArrayCount(descriptors), descriptors, 0, nullptr);
    }

    ScratchEnd(scratch);
}

static void
VK_CameraCleanup(VK_Context* vk_ctx)
{
    for (size_t i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; i++)
    {
        VK_BufferMappedDestroy(vk_ctx->allocator, &vk_ctx->camera_buffer_alloc_mapped[i]);
    }

    vkDestroyDescriptorSetLayout(vk_ctx->device, vk_ctx->camera_descriptor_set_layout, NULL);
}

// ~mgj: Asset Streaming

static VK_AssetManager*
VK_AssetManagerCreate(VkDevice device, U32 queue_family_index, async::Threads* threads,
                      U64 texture_map_size, U64 total_size_in_bytes)
{
    VK_Context* vk_ctx = VK_CtxGet();
    Arena* arena = ArenaAlloc();
    VK_AssetManager* asset_store = PushStruct(arena, VK_AssetManager);
    asset_store->arena = arena;
    asset_store->texture_hashmap =
        BufferAlloc<R_AssetItemList<VK_Texture>>(arena, texture_map_size);
    asset_store->total_size = total_size_in_bytes;
    asset_store->work_queue = threads->msg_queue;
    asset_store->threaded_cmd_pools =
        BufferAlloc<VK_AssetManagerCommandPool>(arena, threads->thread_handles.size);
    asset_store->texture_free_list = nullptr;

    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

    VkCommandPoolCreateInfo cmd_pool_info{};
    cmd_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cmd_pool_info.queueFamilyIndex = queue_family_index;
    cmd_pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;

    for (U32 i = 0; i < threads->thread_handles.size; i++)
    {
        asset_store->threaded_cmd_pools.data[i].cmd_pool =
            VK_CommandPoolCreate(device, &cmd_pool_info);
        asset_store->threaded_cmd_pools.data[i].mutex = OS_MutexAlloc();
    }

    asset_store->cmd_wait_list = VK_AssetManagerCmdListCreate();
    U32 cmd_queue_size = 10;
    asset_store->cmd_queue = async::QueueInit<VK_CmdQueueItem>(
        arena, cmd_queue_size, vk_ctx->queue_family_indices.graphicsFamilyIndex);

    return asset_store;
}
static void
VK_AssetManagerDestroy(VK_Context* vk_ctx, VK_AssetManager* asset_store)
{
    for (U32 i = 0; i < asset_store->threaded_cmd_pools.size; i++)
    {
        vkDestroyCommandPool(vk_ctx->device, asset_store->threaded_cmd_pools.data[i].cmd_pool, 0);
        OS_MutexRelease(asset_store->threaded_cmd_pools.data[i].mutex);
    }
    async::QueueDestroy(asset_store->cmd_queue);
    VK_AssetManagerCmdListDestroy(asset_store->cmd_wait_list);

    ArenaRelease(asset_store->arena);
}

static void
VK_AssetCmdQueueItemEnqueue(U32 thread_id, VkCommandBuffer cmd, R_ThreadInput* thread_input)
{
    VK_AssetManager* asset_manager = VK_CtxGet()->asset_manager;

    VK_CmdQueueItem item = {
        .thread_input = thread_input, .thread_id = thread_id, .cmd_buffer = cmd};
    DEBUG_LOG("Asset ID: %llu - Cmd Getting Queued\n", thread_input->asset_info.info.id.id);
    async::QueuePush(asset_manager->cmd_queue, &item);
}

static void
VK_AssetManagerExecuteCmds()
{
    VK_Context* vk_ctx = VK_CtxGet();
    VK_AssetManager* asset_store = vk_ctx->asset_manager;

    for (U32 i = 0; i < asset_store->cmd_queue->queue_size; i++)
    {
        VK_CmdQueueItem item;
        if (async::QueueTryRead(asset_store->cmd_queue, &item))
        {
            VkSubmitInfo submit_info{};
            submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submit_info.commandBufferCount = 1;
            submit_info.pCommandBuffers = &item.cmd_buffer;

            VkFenceCreateInfo fence_info{};
            fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            VK_CHECK_RESULT(vkCreateFence(vk_ctx->device, &fence_info, nullptr, &item.fence));

            VK_CHECK_RESULT(vkQueueSubmit(vk_ctx->graphics_queue, 1, &submit_info, item.fence));

            DEBUG_LOG("Asset ID: %llu - Submitted Command Buffer\n",
                      item.thread_input->asset_info.info.id.id);
            VK_AssetManagerCmdListAdd(asset_store->cmd_wait_list, item);
        }
    }
}

force_inline static U64
VK_HashIndexFromAssetId(R_AssetId id, U64 hashmap_size)
{
    return id.id % hashmap_size;
}

force_inline static B32
VK_AssetIdCmp(R_AssetId a, R_AssetId b)
{
    return a.id == b.id;
}

static R_AssetItem<VK_Texture>*
VK_AssetManagerTextureItemGet(R_AssetId asset_id)
{
    ScratchScope scratch = ScratchScope(0, 0);
    VK_Context* vk_ctx = VK_CtxGet();
    VK_AssetManager* asset_store = vk_ctx->asset_manager;

    R_AssetItemList<VK_Texture>* texture_list =
        &asset_store->texture_hashmap
             .data[VK_HashIndexFromAssetId(asset_id, asset_store->texture_hashmap.size)];

    return VK_AssetManagerItemGet(asset_store->arena, texture_list, &asset_store->texture_free_list,
                                  asset_id);
}

static R_AssetItem<VK_AssetItemBuffer>*
VK_AssetManagerBufferItemGet(R_AssetId asset_id)
{
    VK_Context* vk_ctx = VK_CtxGet();
    VK_AssetManager* asset_store = vk_ctx->asset_manager;

    return VK_AssetManagerItemGet(asset_store->arena, &asset_store->buffer_list,
                                  &asset_store->buffer_free_list, asset_id);
}

template <typename T>
static R_AssetItem<T>*
VK_AssetManagerItemGet(Arena* arena, R_AssetItemList<T>* list, R_AssetItem<T>** free_list,
                       R_AssetId id)
{
    for (R_AssetItem<T>* buffer_result = list->first; buffer_result;
         buffer_result = buffer_result->next)
    {
        if (VK_AssetIdCmp(buffer_result->id, id))
        {
            return buffer_result;
        }
    }
    R_AssetItem<T>* asset_item = {0};
    if (*free_list)
    {
        asset_item = *free_list;
        SLLStackPop(asset_item);
    }
    else
    {
        asset_item = PushStruct(arena, R_AssetItem<T>);
        asset_item->id = id;
        SLLQueuePushFront(list->first, list->last, asset_item);
    }
    return asset_item;
}

template <typename T>
static void
VK_AssetInfoBufferCmd(VkCommandBuffer cmd, R_AssetId id, Buffer<T> buffer)
{
    VK_Context* vk_ctx = VK_CtxGet();
    R_AssetItem<VK_AssetItemBuffer>* asset_item_buffer = VK_AssetManagerBufferItemGet(id);
    VK_AssetItemBuffer* asset_buffer = &asset_item_buffer->item;

    // ~mgj: copy to staging and record copy command
    U32 buffer_byte_size = buffer.size * sizeof(T);
    VK_BufferAllocation staging_buffer_alloc =
        VK_StagingBufferCreate(vk_ctx->allocator, buffer_byte_size);

    vmaCopyMemoryToAllocation(vk_ctx->allocator, buffer.data, staging_buffer_alloc.allocation, 0,
                              staging_buffer_alloc.size);
    VkBufferCopy copy_region = {0};
    copy_region.size = staging_buffer_alloc.size;
    vkCmdCopyBuffer(cmd, staging_buffer_alloc.buffer, asset_buffer->buffer_alloc.buffer, 1,
                    &copy_region);

    asset_buffer->staging_buffer = staging_buffer_alloc;
}

static void
VK_AssetManagerCmdDoneCheck()
{
    VK_Context* vk_ctx = VK_CtxGet();
    VK_AssetManager* asset_store = vk_ctx->asset_manager;
    for (VK_CmdQueueItem* cmd_queue_item = asset_store->cmd_wait_list->list_first; cmd_queue_item;)
    {
        VK_CmdQueueItem* next = cmd_queue_item->next;
        VkResult result = vkGetFenceStatus(vk_ctx->device, cmd_queue_item->fence);
        if (result == VK_SUCCESS)
        {
            OS_MutexScope(asset_store->threaded_cmd_pools.data[cmd_queue_item->thread_id].mutex)
            {
                vkFreeCommandBuffers(
                    vk_ctx->device,
                    asset_store->threaded_cmd_pools.data[cmd_queue_item->thread_id].cmd_pool, 1,
                    &cmd_queue_item->cmd_buffer);
            }

            R_ThreadInput* thread_input = cmd_queue_item->thread_input;

            R_AssetLoadingInfo* asset_load_info = &thread_input->asset_info;
            if (asset_load_info->info.type == R_AssetItemType_Texture)
            {
                R_AssetItem<VK_Texture>* asset =
                    VK_AssetManagerTextureItemGet(asset_load_info->info.id);
                VK_BufferDestroy(vk_ctx->allocator, &asset->item.staging_buffer);
                asset->is_loaded = 1;
            }
            else if (asset_load_info->info.type == R_AssetItemType_Buffer)
            {
                R_AssetItem<VK_AssetItemBuffer>* asset =
                    VK_AssetManagerBufferItemGet(asset_load_info->info.id);
                VK_BufferDestroy(vk_ctx->allocator, &asset->item.staging_buffer);
                asset->is_loaded = 1;
            }
            Assert(asset_load_info->info.type != R_AssetItemType_Undefined);
            DEBUG_LOG("Asset: %llu - Finished loading\n", asset_load_info->info.id.id);
            vkDestroyFence(vk_ctx->device, cmd_queue_item->fence, 0);
            VK_ThreadInputDestroy(cmd_queue_item->thread_input);
            VK_AssetManagerCmdListItemRemove(asset_store->cmd_wait_list, cmd_queue_item);
        }
        else if (result != VK_NOT_READY)
        {
            VK_CHECK_RESULT(result);
        }
        cmd_queue_item = next;
    }
}

static VkCommandBuffer
VK_BeginCommand(VkDevice device, VK_AssetManagerCommandPool threaded_cmd_pool)
{
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = threaded_cmd_pool.cmd_pool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    OS_MutexScope(threaded_cmd_pool.mutex)
    {
        vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    return commandBuffer;
}

template <typename T>
static B32
IsAssetLoadedOrInProgress(R_AssetItem<T>* asset_item)
{
    return asset_item->is_loaded || asset_item->is_loading;
}

static VK_AssetManagerCmdList*
VK_AssetManagerCmdListCreate()
{
    Arena* arena = ArenaAlloc();
    VK_AssetManagerCmdList* cmd_list = PushStruct(arena, VK_AssetManagerCmdList);
    cmd_list->arena = arena;
    return cmd_list;
}
static void
VK_AssetManagerCmdListDestroy(VK_AssetManagerCmdList* cmd_wait_list)
{
    while (cmd_wait_list->list_first)
    {
        VK_AssetManagerCmdDoneCheck();
    }
    ArenaRelease(cmd_wait_list->arena);
}

static void
VK_AssetManagerCmdListAdd(VK_AssetManagerCmdList* cmd_list, VK_CmdQueueItem item)
{
    VK_CmdQueueItem* item_copy;
    if (cmd_list->free_list)
    {
        item_copy = cmd_list->free_list;
        SLLStackPop(cmd_list->free_list);
    }
    else
    {
        item_copy = PushStruct(cmd_list->arena, VK_CmdQueueItem);
    }
    *item_copy = item;
    DLLPushBack(cmd_list->list_first, cmd_list->list_last, item_copy);
}
static void
VK_AssetManagerCmdListItemRemove(VK_AssetManagerCmdList* cmd_list, VK_CmdQueueItem* item)
{
    DLLRemove(cmd_list->list_first, cmd_list->list_last, item);
    MemoryZeroStruct(item);
    SLLStackPush(cmd_list->free_list, item);
}

static void
VK_AssetManagerBufferFree(R_AssetId asset_id)
{
    VK_Context* vk_ctx = VK_CtxGet();
    R_AssetItem<VK_AssetItemBuffer>* item = VK_AssetManagerBufferItemGet(asset_id);
    if (item->is_loaded)
    {
        VK_BufferDestroy(vk_ctx->allocator, &item->item.buffer_alloc);
    }
    item->is_loaded = false;
}
static void
VK_AssetManagerTextureFree(R_AssetId asset_id)
{
    VK_Context* vk_ctx = VK_CtxGet();
    R_AssetItem<VK_Texture>* item = VK_AssetManagerTextureItemGet(asset_id);
    if (item->is_loaded)
    {
        VK_TextureDestroy(vk_ctx, &item->item);
    }
    item->is_loaded = false;
}

// ~mgj: Rendering

static void
VK_DrawFrameReset()
{
    VK_Context* vk_ctx = VK_CtxGet();
    ArenaClear(vk_ctx->draw_frame_arena);
    vk_ctx->draw_frame = PushStruct(vk_ctx->draw_frame_arena, VK_DrawFrame);
}

static void
VK_PipelineDestroy(VK_Pipeline* pipeline)
{
    VK_Context* vk_ctx = VK_CtxGet();
    vkDestroyPipeline(vk_ctx->device, pipeline->pipeline, NULL);
    vkDestroyPipelineLayout(vk_ctx->device, pipeline->pipeline_layout, NULL);
    vkDestroyDescriptorSetLayout(vk_ctx->device, pipeline->descriptor_set_layout, NULL);
}

static void
VK_Model3DBucketAdd(VK_BufferAllocation* vertex_buffer_allocation,
                    VK_BufferAllocation* index_buffer_allocation, VkDescriptorSet texture_handle,
                    B32 depth_write_per_draw_call_only, U32 index_buffer_offset, U32 index_count)
{
    VK_Context* vk_ctx = VK_CtxGet();
    VK_DrawFrame* draw_frame = vk_ctx->draw_frame;

    VK_Model3DNode* node = PushStruct(vk_ctx->draw_frame_arena, VK_Model3DNode);
    node->vertex_alloc = *vertex_buffer_allocation;
    node->index_alloc = *index_buffer_allocation;
    node->texture_handle = texture_handle;
    node->index_count = index_count;
    node->index_buffer_offset = index_buffer_offset;
    node->depth_write_per_draw_enabled = depth_write_per_draw_call_only;

    SLLQueuePush(draw_frame->model_3D_list.first, draw_frame->model_3D_list.last, node);
}

static void
VK_Model3DInstanceBucketAdd(VK_BufferAllocation* vertex_buffer_allocation,
                            VK_BufferAllocation* index_buffer_allocation,
                            VkDescriptorSet texture_handle, R_BufferInfo* instance_buffer_info)
{
    U32 align = 16;
    VK_Context* vk_ctx = VK_CtxGet();
    VK_DrawFrame* draw_frame = vk_ctx->draw_frame;
    VK_Model3DInstance* instance_draw = &draw_frame->model_3D_instance_draw;

    VK_Model3DInstanceNode* node = PushStruct(vk_ctx->draw_frame_arena, VK_Model3DInstanceNode);
    U32 align_pst = instance_draw->total_instance_buffer_byte_count + (align - 1);
    align_pst -= align_pst % align;
    node->instance_buffer_offset = align_pst;
    node->vertex_alloc = *vertex_buffer_allocation;
    node->index_alloc = *index_buffer_allocation;
    node->texture_handle = texture_handle;
    node->instance_buffer_info = *instance_buffer_info;
    instance_draw->total_instance_buffer_byte_count +=
        node->instance_buffer_offset + instance_buffer_info->buffer.size;
    SLLQueuePush(instance_draw->list.first, instance_draw->list.last, node);
}

static void
VK_Model3DDraw(R_AssetInfo* vertex_info, R_AssetInfo* index_info, R_AssetInfo* texture_info,
               String8 texture_path, R_SamplerInfo* sampler_info, R_BufferInfo* vertex_buffer_info,
               R_BufferInfo* index_buffer_info, B32 depth_test_per_draw_call_only,
               U32 index_buffer_offset, U32 index_count)
{
    R_AssetItem<VK_AssetItemBuffer>* asset_vertex_buffer =
        VK_AssetManagerBufferItemGet(vertex_info->id);
    R_AssetItem<VK_AssetItemBuffer>* asset_index_buffer =
        VK_AssetManagerBufferItemGet(index_info->id);
    R_AssetItem<VK_Texture>* asset_texture = VK_AssetManagerTextureItemGet(texture_info->id);

    if (asset_index_buffer->is_loaded && asset_index_buffer->is_loaded && asset_texture->is_loaded)
    {
        VK_Model3DBucketAdd(&asset_vertex_buffer->item.buffer_alloc,
                            &asset_index_buffer->item.buffer_alloc, asset_texture->item.desc_set,
                            depth_test_per_draw_call_only, index_buffer_offset, index_count);
    }
    else if (!IsAssetLoadedOrInProgress(asset_vertex_buffer) ||
             !IsAssetLoadedOrInProgress(asset_index_buffer) ||
             !IsAssetLoadedOrInProgress(asset_texture))
    {
        if (!IsAssetLoadedOrInProgress(asset_vertex_buffer))
        {
            VK_AssetBufferLoad(vertex_info, vertex_buffer_info);
        }
        if (!IsAssetLoadedOrInProgress(asset_index_buffer))
        {
            VK_AssetBufferLoad(index_info, index_buffer_info);
        }
        if (!IsAssetLoadedOrInProgress(asset_texture))
        {
            VK_AssetTextureLoad(texture_info, sampler_info, texture_path, R_PipelineUsageType_3D);
        }
    }
}

static void
VK_Model3DInstanceDraw(R_AssetInfo* vertex_info, R_AssetInfo* index_info, R_AssetInfo* texture_info,
                       String8 texture_path, R_SamplerInfo* sampler_info,
                       R_BufferInfo* vertex_buffer_info, R_BufferInfo* index_buffer_info,
                       R_BufferInfo* instance_buffer)
{
    R_AssetItem<VK_AssetItemBuffer>* asset_vertex_buffer =
        VK_AssetManagerBufferItemGet(vertex_info->id);
    R_AssetItem<VK_AssetItemBuffer>* asset_index_buffer =
        VK_AssetManagerBufferItemGet(index_info->id);
    R_AssetItem<VK_Texture>* asset_texture = VK_AssetManagerTextureItemGet(texture_info->id);

    if (asset_index_buffer->is_loaded && asset_index_buffer->is_loaded && asset_texture->is_loaded)
    {
        VK_Model3DInstanceBucketAdd(&asset_vertex_buffer->item.buffer_alloc,
                                    &asset_index_buffer->item.buffer_alloc,
                                    asset_texture->item.desc_set, instance_buffer);
    }
    else if (!IsAssetLoadedOrInProgress(asset_vertex_buffer) ||
             !IsAssetLoadedOrInProgress(asset_index_buffer) ||
             !IsAssetLoadedOrInProgress(asset_texture))
    {
        if (!IsAssetLoadedOrInProgress(asset_vertex_buffer))
        {
            VK_AssetBufferLoad(vertex_info, vertex_buffer_info);
        }
        if (!IsAssetLoadedOrInProgress(asset_index_buffer))
        {
            VK_AssetBufferLoad(index_info, index_buffer_info);
        }
        if (!IsAssetLoadedOrInProgress(asset_texture))
        {
            VK_AssetTextureLoad(texture_info, sampler_info, texture_path,
                                R_PipelineUsageType_3DInstanced);
        }
    }
}

enum WriteType
{
    WriteType_Color,
    WriteType_Depth,
    WriteType_Count
};

static void
VK_Model3DRendering()
{
    VK_Context* vk_ctx = VK_CtxGet();
    VK_Pipeline* model_3D_pipeline = &vk_ctx->model_3D_pipeline;
    VK_DrawFrame* draw_frame = vk_ctx->draw_frame;

    VK_SwapchainResources* swapchain_resources = vk_ctx->swapchain_resources;
    VkExtent2D swapchain_extent = swapchain_resources->swapchain_extent;
    VkCommandBuffer cmd_buffer = vk_ctx->command_buffers.data[vk_ctx->current_frame];

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

    VkBool32 color_write_enabled[4] = {VK_TRUE, VK_TRUE, VK_TRUE, VK_TRUE};
    VkBool32 color_write_disabled[4] = {};

    VkDescriptorSet descriptor_sets[2] = {vk_ctx->camera_descriptor_sets[vk_ctx->current_frame]};

    VkDeviceSize offsets[] = {0};
    for (VK_Model3DNode* node = draw_frame->model_3D_list.first; node; node = node->next)
    {
        descriptor_sets[1] = node->texture_handle;
        vkCmdBindDescriptorSets(cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                model_3D_pipeline->pipeline_layout, 0, ArrayCount(descriptor_sets),
                                descriptor_sets, 0, NULL);
        vkCmdBindVertexBuffers(cmd_buffer, 0, 1, &node->vertex_alloc.buffer, offsets);
        vkCmdBindIndexBuffer(cmd_buffer, node->index_alloc.buffer, 0, VK_INDEX_TYPE_UINT32);
        if (node->depth_write_per_draw_enabled)
        {
            for (WriteType write_type = (WriteType)0; write_type < WriteType_Count;
                 write_type = (WriteType)(write_type + 1))
            {
                if (write_type == WriteType_Color)
                {
                    vkCmdSetDepthWriteEnable(cmd_buffer, VK_FALSE);
                    vkCmdSetColorWriteEnableExt(cmd_buffer, ArrayCount(color_write_enabled),
                                                color_write_enabled);
                }
                else if (write_type == WriteType_Depth)
                {
                    vkCmdSetDepthWriteEnable(cmd_buffer, VK_TRUE);
                    vkCmdSetColorWriteEnableExt(cmd_buffer, ArrayCount(color_write_disabled),
                                                color_write_disabled);
                }

                vkCmdDrawIndexed(cmd_buffer, node->index_count, 1, node->index_buffer_offset, 0, 0);
            }
        }
        else
        {
            vkCmdSetDepthWriteEnable(cmd_buffer, VK_TRUE);
            vkCmdSetColorWriteEnableExt(cmd_buffer, ArrayCount(color_write_enabled),
                                        color_write_enabled);
            vkCmdDrawIndexed(cmd_buffer, node->index_count, 1, node->index_buffer_offset, 0, 0);
        }
    }
}

static void
VK_CommandBufferRecord(U32 image_index, U32 current_frame, ui::Camera* camera,
                       Vec2S64 mouse_cursor_pos)
{
    ProfScopeMarker;
    Temp scratch = ScratchBegin(0, 0);

    VK_Context* vk_ctx = VK_CtxGet();

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = 0;                  // Optional
    beginInfo.pInheritanceInfo = nullptr; // Optional

    VK_SwapchainResources* swapchain_resource = vk_ctx->swapchain_resources;

    // object id color attachment image
    VK_ImageResource* object_id_image_resource =
        &swapchain_resource->object_id_image_resources.data[image_index];
    VK_ImageAllocation* object_id_image_alloc = &object_id_image_resource->image_alloc;
    VkImageView object_id_image_view = object_id_image_resource->image_view_resource.image_view;
    VkImage object_id_image = object_id_image_alloc->image;

    // object id resolve image
    VK_ImageResource* object_id_image_resolve_resource =
        &swapchain_resource->object_id_image_resolve_resources.data[image_index];
    VkImageView object_id_image_resolve_view =
        object_id_image_resolve_resource->image_view_resource.image_view;
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
            exitWithError("failed to begin recording command buffer!");
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

        VkImageMemoryBarrier2 pre_render_object_id_barrier{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
            .srcAccessMask = VK_ACCESS_2_NONE,
            .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .image = object_id_image,
            .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                 .baseMipLevel = 0,
                                 .levelCount = 1,
                                 .baseArrayLayer = 0,
                                 .layerCount = 1}};

        VkImageMemoryBarrier2 pre_render_object_id_resolve_image_barrier{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
            .srcAccessMask = VK_ACCESS_2_NONE,
            .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .image = object_id_resolve_image,
            .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                 .baseMipLevel = 0,
                                 .levelCount = 1,
                                 .baseArrayLayer = 0,
                                 .layerCount = 1}};

        VkImageMemoryBarrier2 pre_render_barriers[] = {pre_render_object_id_barrier,
                                                       pre_render_swapchain_barrier,
                                                       pre_render_object_id_resolve_image_barrier};
        VkDependencyInfo pre_render_transition_info = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                                       .imageMemoryBarrierCount =
                                                           ArrayCount(pre_render_barriers),
                                                       .pImageMemoryBarriers = pre_render_barriers};

        vkCmdPipelineBarrier2(current_cmd_buf, &pre_render_transition_info);
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

            VkClearColorValue clear_color = {0.0f, 0.0f, 0.0f, 1.0f};
            VkClearDepthStencilValue clear_depth = {1.0f, 0};
            VkImageSubresourceRange image_range = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                                   .baseMipLevel = 0,
                                                   .levelCount = 1,
                                                   .baseArrayLayer = 0,
                                                   .layerCount = 1};

            // ~mgj: Transition color attachment image layout to
            // VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL to render into
            VkRenderingAttachmentInfo color_attachment{};
            color_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            color_attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            color_attachment.clearValue = {.color = clear_color};

            // Object ID attachment
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

            // Depth attachment
            VkRenderingAttachmentInfo depth_attachment{};
            depth_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            depth_attachment.imageView = depth_image_view;
            depth_attachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            depth_attachment.clearValue = {.depthStencil = clear_depth};

            VkRenderingAttachmentInfo color_attachments[] = {color_attachment,
                                                             object_id_attachment};
            VkRenderingInfo rendering_info{};
            rendering_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            rendering_info.renderArea.offset = {0, 0};
            rendering_info.renderArea.extent = swapchain_extent;
            rendering_info.layerCount = 1;
            rendering_info.colorAttachmentCount = ArrayCount(color_attachments);
            rendering_info.pColorAttachments = color_attachments;
            rendering_info.pDepthAttachment = &depth_attachment;

            VK_CameraUniformBufferUpdate(
                vk_ctx, camera,
                Vec2F32{(F32)vk_ctx->swapchain_resources->swapchain_extent.width,
                        (F32)vk_ctx->swapchain_resources->swapchain_extent.height},
                current_frame);

            vkCmdBeginRendering(current_cmd_buf, &rendering_info);

            VK_Model3DInstanceRendering();
            VK_Model3DRendering();

            vkCmdEndRendering(current_cmd_buf);

            // ~mgj: Render ImGui
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
            const bool is_minimized =
                (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f);
            if (!is_minimized)
            {
                ImGui_ImplVulkan_RenderDrawData(draw_data, current_cmd_buf);
            }

            vkCmdEndRendering(current_cmd_buf);

            // ~mgj: Transition color attachment images for presentation or transfer
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

            VkImageMemoryBarrier2 post_render_barriers[] = {present_barrier,
                                                            object_id_read_barrier};
            VkDependencyInfo layout_transition_info = {
                .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .imageMemoryBarrierCount = ArrayCount(post_render_barriers),
                .pImageMemoryBarriers = post_render_barriers};

            vkCmdPipelineBarrier2(current_cmd_buf, &layout_transition_info);

            // ~mgj: Read object id from mouse position
            Vec2S64 mouse_position_screen_coords = mouse_cursor_pos;
            if (mouse_position_screen_coords.x > 0 && mouse_position_screen_coords.y > 0 &&
                mouse_position_screen_coords.x <
                    vk_ctx->swapchain_resources->swapchain_extent.width &&
                mouse_position_screen_coords.y <
                    vk_ctx->swapchain_resources->swapchain_extent.height)
            {
                VkBufferImageCopy buffer_image_copy[] = {{
                    .imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                         .mipLevel = 0,
                                         .baseArrayLayer = 0,
                                         .layerCount = 1},
                    .imageOffset = {(S32)mouse_position_screen_coords.x,
                                    (S32)mouse_position_screen_coords.y, 0},
                    .imageExtent = {1, 1, 1},
                }};
                vkCmdCopyImageToBuffer(
                    current_cmd_buf, object_id_resolve_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    swapchain_resource->object_id_buffer_readback.buffer_alloc.buffer,
                    ArrayCount(buffer_image_copy), buffer_image_copy);

                Assert(vk_ctx->object_id_format == VK_FORMAT_R32G32_UINT);
                U64* object_id = (U64*)swapchain_resource->object_id_buffer_readback.mapped_ptr;
                vk_ctx->hovered_object_id = *object_id;
            }

            // ~mgj: Reset object ID resolve image layout to
            // VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
            VkImageMemoryBarrier2 object_id_reset_barrier = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                .srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_2_NONE,
                .dstAccessMask = VK_ACCESS_2_NONE,
                .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = object_id_resolve_image,
                .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                     .baseMipLevel = 0,
                                     .levelCount = 1,
                                     .baseArrayLayer = 0,
                                     .layerCount = 1}};

            VkDependencyInfo layout_reset_transition_info = {
                .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .imageMemoryBarrierCount = 1,
                .pImageMemoryBarriers = &object_id_reset_barrier};

            vkCmdPipelineBarrier2(current_cmd_buf, &layout_reset_transition_info);
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
static void
VK_ProfileBuffersCreate(VK_Context* vk_ctx)
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
VK_ProfileBuffersDestroy(VK_Context* vk_ctx)
{
#ifdef TRACY_ENABLE
    vkQueueWaitIdle(vk_ctx->graphics_queue);
    for (U32 i = 0; i < ArrayCount(vk_ctx->tracy_ctx); i++)
    {
        TracyVkDestroy(vk_ctx->tracy_ctx[i]);
    }
#endif
}
// ~mgj: Vulkan Interface
static void
R_RenderCtxCreate(String8 shader_path, IO* io_ctx, async::Threads* thread_pool)
{
    ScratchScope scratch = ScratchScope(0, 0);

    Arena* arena = ArenaAlloc();

    VK_Context* vk_ctx = PushStruct(arena, VK_Context);
    VK_CtxSet(vk_ctx);
    vk_ctx->arena = arena;

    const char* validation_layers[] = {"VK_LAYER_KHRONOS_validation",
                                       "VK_LAYER_KHRONOS_synchronization2"};
    vk_ctx->validation_layers = BufferAlloc<String8>(vk_ctx->arena, ArrayCount(validation_layers));
    for (U32 i = 0; i < ArrayCount(validation_layers); i++)
    {
        vk_ctx->validation_layers.data[i] = {Str8CString(validation_layers[i])};
    }

    const char* device_extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME,
                                       VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
                                       VK_EXT_COLOR_WRITE_ENABLE_EXTENSION_NAME};
    vk_ctx->device_extensions = BufferAlloc<String8>(vk_ctx->arena, ArrayCount(device_extensions));
    for (U32 i = 0; i < ArrayCount(device_extensions); i++)
    {
        vk_ctx->device_extensions.data[i] = {Str8CString(device_extensions[i])};
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
        exitWithError("texture image format does not support linear blitting!");
    }

    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.physicalDevice = vk_ctx->physical_device;
    allocatorInfo.device = vk_ctx->device;
    allocatorInfo.instance = vk_ctx->instance;

    vmaCreateAllocator(&allocatorInfo, &vk_ctx->allocator);
    vk_ctx->object_id_format = VK_FORMAT_R32G32_UINT;

    Vec2S32 vk_framebuffer_dim_s32 = IO_WaitForValidFramebufferSize(io_ctx);
    Vec2U32 vk_framebuffer_dim_u32 = {(U32)vk_framebuffer_dim_s32.x, (U32)vk_framebuffer_dim_s32.y};
    vk_ctx->swapchain_resources = VK_SwapChainCreate(vk_ctx, vk_framebuffer_dim_u32);

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = vk_ctx->queue_family_indices.graphicsFamilyIndex;
    vk_ctx->command_pool = VK_CommandPoolCreate(vk_ctx->device, &poolInfo);

    VK_CommandBuffersCreate(vk_ctx);

    VK_SyncObjectsCreate(vk_ctx);

    VK_DescriptorPoolCreate(vk_ctx);
    VK_CameraUniformBufferCreate(vk_ctx);
    VK_CameraDescriptorSetLayoutCreate(vk_ctx);
    VK_CameraDescriptorSetCreate(vk_ctx);
    VK_ProfileBuffersCreate(vk_ctx);

    // TODO: change from 1 to much larger value
    vk_ctx->asset_manager = VK_AssetManagerCreate(
        vk_ctx->device, vk_ctx->queue_family_indices.graphicsFamilyIndex, thread_pool, 1, GB(1));

    // ~mgj: Drawing (TODO: Move out of vulkan context to own module)
    vk_ctx->draw_frame_arena = ArenaAlloc();
    vk_ctx->model_3D_pipeline = VK_Model3DPipelineCreate(vk_ctx, shader_path);
    vk_ctx->model_3D_instance_pipeline = VK_Model3DInstancePipelineCreate(vk_ctx, shader_path);
}
static void
R_RenderCtxDestroy()
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

    VK_SwapChainCleanup(vk_ctx->device, vk_ctx->allocator, vk_ctx->swapchain_resources);

    vkDestroyCommandPool(vk_ctx->device, vk_ctx->command_pool, nullptr);

    vkDestroySurfaceKHR(vk_ctx->instance, vk_ctx->surface, nullptr);
    for (U32 i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; i++)
    {
        vkDestroySemaphore(vk_ctx->device, vk_ctx->render_finished_semaphores.data[i], nullptr);
        vkDestroySemaphore(vk_ctx->device, vk_ctx->image_available_semaphores.data[i], nullptr);
        vkDestroyFence(vk_ctx->device, vk_ctx->in_flight_fences.data[i], nullptr);
    }

    vkDestroyDescriptorPool(vk_ctx->device, vk_ctx->descriptor_pool, 0);

    VK_BufferDestroy(vk_ctx->allocator, &vk_ctx->model_3D_instance_buffer);

    vmaDestroyAllocator(vk_ctx->allocator);

    VK_AssetManagerDestroy(vk_ctx, vk_ctx->asset_manager);

    VK_PipelineDestroy(&vk_ctx->model_3D_pipeline);
    VK_PipelineDestroy(&vk_ctx->model_3D_instance_pipeline);

    vkDestroyDevice(vk_ctx->device, nullptr);
    vkDestroyInstance(vk_ctx->instance, nullptr);

    ArenaRelease(vk_ctx->draw_frame_arena);

    ArenaRelease(vk_ctx->arena);
}

static void
R_RenderFrame(Vec2U32 framebuffer_dim, B32* in_out_framebuffer_resized, ui::Camera* camera,
              Vec2S64 mouse_cursor_pos)
{
    VK_Context* vk_ctx = VK_CtxGet();

    VK_AssetManagerExecuteCmds();
    VK_AssetManagerCmdDoneCheck();
    VkSemaphore image_available_semaphore =
        vk_ctx->image_available_semaphores.data[vk_ctx->current_frame];
    VkSemaphore render_finished_semaphore =
        vk_ctx->render_finished_semaphores.data[vk_ctx->current_frame];
    VkFence* in_flight_fence = &vk_ctx->in_flight_fences.data[vk_ctx->current_frame];
    {
        ProfScopeMarkerNamed("Wait for frame");
        VK_CHECK_RESULT(vkWaitForFences(vk_ctx->device, 1, in_flight_fence, VK_TRUE, 1000000000));
    }

    uint32_t imageIndex;
    VkResult result =
        vkAcquireNextImageKHR(vk_ctx->device, vk_ctx->swapchain_resources->swapchain, UINT64_MAX,
                              image_available_semaphore, VK_NULL_HANDLE, &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR ||
        *in_out_framebuffer_resized)
    {
        *in_out_framebuffer_resized = FALSE;
        ImGui::EndFrame();

        VK_RecreateSwapChain(framebuffer_dim, vk_ctx);
        vk_ctx->current_frame = (vk_ctx->current_frame + 1) % VK_MAX_FRAMES_IN_FLIGHT;
        return;
    }
    else if (result != VK_SUCCESS)
    {
        exitWithError("failed to acquire swap chain image!");
    }

    VkCommandBuffer cmd_buffer = vk_ctx->command_buffers.data[vk_ctx->current_frame];
    VK_CHECK_RESULT(vkResetFences(vk_ctx->device, 1, in_flight_fence));
    VK_CHECK_RESULT(vkResetCommandBuffer(cmd_buffer, 0));

    VK_CommandBufferRecord(imageIndex, vk_ctx->current_frame, camera, mouse_cursor_pos);

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
        *in_out_framebuffer_resized)
    {
        *in_out_framebuffer_resized = 0;
        VK_RecreateSwapChain(framebuffer_dim, vk_ctx);
    }
    else if (result != VK_SUCCESS)
    {
        exitWithError("failed to present swap chain image!");
    }

    vk_ctx->current_frame = (vk_ctx->current_frame + 1) % VK_MAX_FRAMES_IN_FLIGHT;
}

static void
R_GpuWorkDoneWait()
{
    VK_Context* vk_ctx = VK_CtxGet();
    vkDeviceWaitIdle(vk_ctx->device);
}

static void
R_NewFrame()
{
    VK_Context* vk_ctx = VK_CtxGet();
    ArenaClear(vk_ctx->draw_frame_arena);
    vk_ctx->draw_frame = PushStruct(vk_ctx->draw_frame_arena, VK_DrawFrame);
    ImGui_ImplVulkan_NewFrame();
}

static U64
R_LatestHoveredObjectIdGet()
{
    VK_Context* vk_ctx = VK_CtxGet();
    return vk_ctx->hovered_object_id;
}

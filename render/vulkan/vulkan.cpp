

namespace wrapper
{
// ~mgj: global context
static VulkanContext* g_vk_ctx = 0;

// ~mgj: vulkan context
static void
VulkanDestroy(VulkanContext* vk_ctx)
{
    vkDeviceWaitIdle(vk_ctx->device);

    if (vk_ctx->enable_validation_layers)
    {
        DestroyDebugUtilsMessengerEXT(vk_ctx->instance, vk_ctx->debug_messenger, nullptr);
    }

    CameraCleanup(vk_ctx);

    VK_SwapChainCleanup(vk_ctx->device, vk_ctx->allocator, vk_ctx->swapchain_resources);

    vkDestroyCommandPool(vk_ctx->device, vk_ctx->command_pool, nullptr);

    vkDestroySurfaceKHR(vk_ctx->instance, vk_ctx->surface, nullptr);
    for (U32 i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        vkDestroySemaphore(vk_ctx->device, vk_ctx->render_finished_semaphores.data[i], nullptr);
        vkDestroySemaphore(vk_ctx->device, vk_ctx->image_available_semaphores.data[i], nullptr);
        vkDestroyFence(vk_ctx->device, vk_ctx->in_flight_fences.data[i], nullptr);
    }

    vkDestroyDescriptorPool(vk_ctx->device, vk_ctx->descriptor_pool, 0);

    BufferDestroy(vk_ctx->allocator, &vk_ctx->model_3D_instance_buffer);

    vmaDestroyAllocator(vk_ctx->allocator);

    AssetManagerDestroy(vk_ctx, vk_ctx->asset_manager);

    PipelineDestroy(&vk_ctx->model_3D_pipeline);
    PipelineDestroy(&vk_ctx->model_3D_instance_pipeline);

    vkDestroyDevice(vk_ctx->device, nullptr);
    vkDestroyInstance(vk_ctx->instance, nullptr);

    ArenaRelease(vk_ctx->draw_frame_arena);

    ArenaRelease(vk_ctx->arena);
}

static VulkanContext*
VulkanCreate(Context* ctx)
{
    IO* io_ctx = ctx->io;

    Temp scratch = ScratchBegin(0, 0);

    Arena* arena = ArenaAlloc();

    VulkanContext* vk_ctx = PushStruct(arena, VulkanContext);
    wrapper::VulkanCtxSet(vk_ctx);
    vk_ctx->arena = arena;
    vk_ctx->texture_path = Str8PathFromStr8List(arena, {ctx->cwd, S("textures")});
    vk_ctx->shader_path = Str8PathFromStr8List(arena, {ctx->cwd, S("shaders")});
    vk_ctx->asset_path = Str8PathFromStr8List(arena, {ctx->cwd, S("assets")});

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
    vk_ctx->swapchain_resources = VK_SwapChainCreate(vk_ctx, io_ctx);

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = vk_ctx->queue_family_indices.graphicsFamilyIndex;
    vk_ctx->command_pool = VK_CommandPoolCreate(vk_ctx->device, &poolInfo);

    VK_CommandBuffersCreate(vk_ctx);

    VK_SyncObjectsCreate(vk_ctx);

    DescriptorPoolCreate(vk_ctx);
    CameraUniformBufferCreate(vk_ctx);
    CameraDescriptorSetLayoutCreate(vk_ctx);
    CameraDescriptorSetCreate(vk_ctx);

    // TODO: change from 1 to much larger value
    vk_ctx->asset_manager =
        AssetManagerCreate(vk_ctx->device, vk_ctx->queue_family_indices.graphicsFamilyIndex,
                           ctx->thread_info, 1, GB(1));

    // ~mgj: Rendering
    vk_ctx->draw_frame_arena = ArenaAlloc();
    vk_ctx->model_3D_pipeline = Model3DPipelineCreate(vk_ctx);
    vk_ctx->model_3D_instance_pipeline = Model3DInstancePipelineCreate(vk_ctx);

    ScratchEnd(scratch);

    return vk_ctx;
}

static void
VulkanCtxSet(VulkanContext* vk_ctx)
{
    Assert(!g_vk_ctx);
    g_vk_ctx = vk_ctx;
}

static VulkanContext*
VulkanCtxGet()
{
    Assert(g_vk_ctx);
    return g_vk_ctx;
}

// ~mgj: Car function

static ImageKtx2*
ImageFromKtx2file(VkCommandBuffer cmd, BufferAllocation staging_buffer, VulkanContext* vk_ctx,
                  ktxTexture2* ktx_texture)
{
    ScratchScope scratch = ScratchScope(0, 0);

    U32 img_width = ktx_texture->baseWidth;
    U32 img_height = ktx_texture->baseHeight;
    VkDeviceSize img_size = ktx_texture->dataSize;
    U32 mip_levels = ktx_texture->numLevels;

    vmaCopyMemoryToAllocation(vk_ctx->allocator, ktx_texture->pData, staging_buffer.allocation, 0,
                              img_size);

    VmaAllocationCreateInfo vma_info = {.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};

    ImageAllocation image_alloc =
        ImageAllocationCreate(vk_ctx->allocator, img_width, img_height, VK_SAMPLE_COUNT_1_BIT,
                              VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL,
                              VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                  VK_IMAGE_USAGE_SAMPLED_BIT,
                              mip_levels, vma_info);

    VkCommandPoolCreateInfo cmd_pool_create_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = vk_ctx->queue_family_indices.graphicsFamilyIndex,

    };

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
        .image = image_alloc.image,
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                             .baseMipLevel = 0,
                             .levelCount = mip_levels,
                             .baseArrayLayer = 0,
                             .layerCount = 1},
    };

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                         0, NULL, 0, NULL, 1, &barrier);

    vkCmdCopyBufferToImage(cmd, staging_buffer.buffer, image_alloc.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mip_levels, regions);

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, NULL, 0, NULL, 1, &barrier);

    // create texture
    ImageViewResource image_view_resource =
        ImageViewResourceCreate(vk_ctx->device, image_alloc.image, VK_FORMAT_R8G8B8A8_SRGB,
                                VK_IMAGE_ASPECT_COLOR_BIT, mip_levels);

    ImageResource image_resource = {.image_alloc = image_alloc,
                                    .image_view_resource = image_view_resource};

    ImageKtx2* image_ktx = PushStruct(vk_ctx->arena, ImageKtx2);
    image_ktx->image_resource = image_resource;
    image_ktx->height = img_height;
    image_ktx->width = img_width;
    image_ktx->mip_level_count = mip_levels;

    return image_ktx;
}

static void
AssetLoad(R_AssetLoadingInfoNodeList* asset_loading_wait_list,
          R_AssetLoadingInfoNode* asset_info_node)
{
    SLLQueuePush(asset_loading_wait_list->first, asset_loading_wait_list->last, asset_info_node);
    asset_loading_wait_list->count++;
}

static void
AssetBufferLoad(Arena* arena, R_AssetItem<AssetItemBuffer>* asset_item,
                R_AssetLoadingInfoNodeList* asset_loading_wait_list, R_AssetInfo* asset_info,
                R_BufferInfo* buffer_info)
{
    R_AssetLoadingInfoNode* node = PushStruct(arena, R_AssetLoadingInfoNode);
    R_AssetLoadingInfo* asset_load_info = &node->load_info;
    asset_load_info->info = *asset_info;
    R_BufferInfo* buffer_load_info = &asset_load_info->extra_info.buffer_info;
    *buffer_load_info = *buffer_info;

    AssetLoad(asset_loading_wait_list, node);
    asset_item->is_loading = TRUE;
}

static void
AssetTextureLoad(Arena* arena, R_AssetItem<Texture>* asset_item,
                 R_AssetLoadingInfoNodeList* asset_loading_wait_list, R_AssetInfo* asset_info,
                 R_SamplerInfo* sampler_info, String8 texture_path)
{
    R_AssetLoadingInfoNode* node = PushStruct(arena, R_AssetLoadingInfoNode);
    R_AssetLoadingInfo* asset_load_info = &node->load_info;
    asset_load_info->info = *asset_info;
    R_TextureLoadingInfo* texture_load_info = &asset_load_info->extra_info.texture_info;
    texture_load_info->sampler_info = *sampler_info;
    texture_load_info->texture_path = PushStr8Copy(arena, texture_path);

    AssetLoad(asset_loading_wait_list, node);
    asset_item->is_loading = TRUE;
}

static R_ThreadInput*
ThreadInputCreate()
{
    Arena* arena = ArenaAlloc();
    R_ThreadInput* thread_input = PushStruct(arena, R_ThreadInput);
    thread_input->arena = arena;
    return thread_input;
}

static void
ThreadInputDestroy(R_ThreadInput* thread_input)
{
    ArenaRelease(thread_input->arena);
}

static void
ThreadSetup(async::ThreadInfo thread_info, void* input)
{
    R_ThreadInput* thread_input = (R_ThreadInput*)input;

    VulkanContext* vk_ctx = wrapper::VulkanCtxGet();
    AssetManager* asset_store = vk_ctx->asset_manager;

    // ~mgj: Record the command buffer
    VkCommandBuffer cmd = BeginCommand(
        vk_ctx->device, asset_store->threaded_cmd_pools.data[thread_info.thread_id]); // Your helper

    for (R_AssetLoadingInfoNode* node = thread_input->asset_loading_wait_list.first; node;
         node = node->next)
    {
        R_AssetLoadingInfo* asset_loading_info = &node->load_info;
        Assert(asset_loading_info->info.id.id != 0);

        switch (asset_loading_info->info.type)
        {
        case R_AssetItemType_Texture:
        {
            R_TextureLoadingInfo* extra_info =
                (R_TextureLoadingInfo*)&asset_loading_info->extra_info;
            TextureCreate(cmd, asset_loading_info->info, extra_info->texture_path,
                          extra_info->sampler_info);
        }
        break;
        case R_AssetItemType_Buffer:
        {
            VkBufferUsageFlagBits buffer_usage_flags = {};
            switch (asset_loading_info->info.pipeline_usage_type)
            {
            case R_PipelineUsageType_VertexBuffer:
                buffer_usage_flags = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
                break;
            case R_PipelineUsageType_IndexBuffer:
                buffer_usage_flags = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
                break;
            default:
                DEBUG_LOG("Unknown pipeline usage type for buffer: %d",
                          asset_loading_info->info.pipeline_usage_type);
                Assert(0);
                continue;
            };
            R_BufferInfo* buffer_info = (R_BufferInfo*)&asset_loading_info->extra_info;
            R_AssetInfo vertex_buffer_info = AssetInfoBufferCmd(
                cmd, asset_loading_info->info.id, buffer_info->buffer, buffer_usage_flags);
        }
        break;
        default:
            Assert(0);
            continue;
        }
        ins_atomic_u32_eval_assign(&asset_loading_info->is_loaded, TRUE);
    }
    VK_CHECK_RESULT(vkEndCommandBuffer(cmd));

    // ~mgj: Enqueue the command buffer
    AssetCmdQueueItemEnqueue(thread_info.thread_id, cmd, thread_input);
}

static void
TextureCreate(VkCommandBuffer cmd_buffer, R_AssetInfo asset_info, String8 texture_path,
              R_SamplerInfo sampler_info)
{
    VulkanContext* vk_ctx = wrapper::VulkanCtxGet();
    R_AssetItem<Texture>* asset_store_texture = AssetManagerTextureItemGet(asset_info.id);
    Texture* texture = &asset_store_texture->item;

    VkSamplerCreateInfo sampler_create_info = {};
    VkSamplerCreateInfoFromSamplerInfo(&sampler_info, &sampler_create_info);
    sampler_create_info.anisotropyEnable = VK_TRUE;
    sampler_create_info.maxAnisotropy = (F32)vk_ctx->msaa_samples;
    VkSampler vk_sampler = SamplerCreate(vk_ctx->device, &sampler_create_info);

    // Get texture
    ktxTexture2* ktx_texture;
    ktx_error_code_e ktxresult = ktxTexture2_CreateFromNamedFile(
        (char*)texture_path.str, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &ktx_texture);
    Assert(ktxresult == KTX_SUCCESS);

    BufferAllocation texture_staging_buffer =
        StagingBufferCreate(vk_ctx->allocator, ktx_texture->dataSize);

    ImageKtx2* image_ktx =
        ImageFromKtx2file(cmd_buffer, texture_staging_buffer, vk_ctx, ktx_texture);

    texture->image_resource = image_ktx->image_resource;
    texture->sampler = vk_sampler;
    texture->staging_buffer = texture_staging_buffer;
}

static Pipeline
Model3DInstancePipelineCreate(VulkanContext* vk_ctx)
{
    ScratchScope scratch = ScratchScope(0, 0);

    VkDescriptorSetLayoutBinding desc_set_layout_info = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
    };
    VkDescriptorSetLayout desc_set_layout =
        DescriptorSetLayoutCreate(vk_ctx->device, &desc_set_layout_info, 1);

    String8 vert_path = CreatePathFromStrings(
        scratch.arena,
        Str8BufferFromCString(scratch.arena, {(char*)vk_ctx->shader_path.str, "model_3d_instancing",
                                              "model_3d_instancing_vert.spv"}));
    String8 frag_path = CreatePathFromStrings(
        scratch.arena,
        Str8BufferFromCString(scratch.arena, {(char*)vk_ctx->shader_path.str, "model_3d_instancing",
                                              "model_3d_instancing_frag.spv"}));

    ShaderModuleInfo vert_shader_stage_info =
        ShaderStageFromSpirv(scratch.arena, vk_ctx->device, VK_SHADER_STAGE_VERTEX_BIT, vert_path);
    ShaderModuleInfo frag_shader_stage_info = ShaderStageFromSpirv(
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

    Pipeline pipeline_info = {.pipeline = pipeline,
                              .pipeline_layout = pipeline_layout,
                              .descriptor_set_layout = desc_set_layout};
    return pipeline_info;
}

static Pipeline
Model3DPipelineCreate(VulkanContext* vk_ctx)
{
    ScratchScope scratch = ScratchScope(0, 0);

    VkDescriptorSetLayoutBinding desc_set_layout_info = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
    };

    VkDescriptorSetLayout desc_set_layout =
        DescriptorSetLayoutCreate(vk_ctx->device, &desc_set_layout_info, 1);

    String8 vert_path = CreatePathFromStrings(
        scratch.arena, Str8BufferFromCString(scratch.arena, {(char*)vk_ctx->shader_path.str,
                                                             "model_3d", "model_3d_vert.spv"}));
    String8 frag_path = CreatePathFromStrings(
        scratch.arena, Str8BufferFromCString(scratch.arena, {(char*)vk_ctx->shader_path.str,
                                                             "model_3d", "model_3d_frag.spv"}));

    ShaderModuleInfo vert_shader_stage_info =
        ShaderStageFromSpirv(scratch.arena, vk_ctx->device, VK_SHADER_STAGE_VERTEX_BIT, vert_path);
    ShaderModuleInfo frag_shader_stage_info = ShaderStageFromSpirv(
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

    Pipeline pipeline_info = {.pipeline = pipeline,
                              .pipeline_layout = pipeline_layout,
                              .descriptor_set_layout = desc_set_layout};
    return pipeline_info;
}

static void
Model3DInstanceRendering()
{
    VulkanContext* vk_ctx = VulkanCtxGet();
    SwapchainResources* swapchain_resources = vk_ctx->swapchain_resources;
    VkExtent2D swapchain_extent = swapchain_resources->swapchain_extent;
    VkCommandBuffer cmd_buffer = vk_ctx->command_buffers.data[vk_ctx->current_frame];
    Pipeline* pipeline = &vk_ctx->model_3D_instance_pipeline;

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
    BufferAllocation* instance_buffer_alloc = &vk_ctx->model_3D_instance_buffer;
    Model3DInstance* model_3D_instance_draw = &vk_ctx->draw_frame->model_3D_instance_draw;

    VkDescriptorSet descriptor_sets[2] = {vk_ctx->camera_descriptor_sets[vk_ctx->current_frame]};
    BufferAllocCreateOrResize(vk_ctx->allocator,
                              model_3D_instance_draw->total_instance_buffer_byte_count,
                              instance_buffer_alloc, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

    for (Model3DInstanceNode* node = vk_ctx->draw_frame->model_3D_instance_draw.list.first; node;
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
        descriptor_sets[1] = {(VkDescriptorSet)R_HandleToPtr(node->texture_handle)};
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
CameraUniformBufferCreate(VulkanContext* vk_ctx)
{
    VkDeviceSize camera_buffer_size = sizeof(CameraUniformBuffer);
    VkBufferUsageFlags buffer_usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

    for (U32 i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        vk_ctx->camera_buffer_alloc_mapped[i] =
            BufferMappedCreate(vk_ctx->allocator, camera_buffer_size, buffer_usage);
    }
}

static void
CameraUniformBufferUpdate(VulkanContext* vk_ctx, ui::Camera* camera, Vec2F32 screen_res,
                          U32 current_frame)
{
    BufferAllocationMapped* buffer = &vk_ctx->camera_buffer_alloc_mapped[current_frame];
    CameraUniformBuffer* ubo = (CameraUniformBuffer*)buffer->mapped_ptr;
    glm::mat4 transform = camera->projection_matrix * camera->view_matrix;
    FrustumPlanesCalculate(&ubo->frustum, transform);
    ubo->viewport_dim.x = screen_res.x;
    ubo->viewport_dim.y = screen_res.y;
    ubo->view = camera->view_matrix;
    ubo->proj = camera->projection_matrix;

    BufferMappedUpdate(vk_ctx->command_buffers.data[current_frame], vk_ctx->allocator, *buffer);
}

static void
CameraDescriptorSetLayoutCreate(VulkanContext* vk_ctx)
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
CameraDescriptorSetCreate(VulkanContext* vk_ctx)
{
    Temp scratch = ScratchBegin(0, 0);
    Arena* arena = scratch.arena;

    Buffer<VkDescriptorSetLayout> layouts =
        BufferAlloc<VkDescriptorSetLayout>(arena, MAX_FRAMES_IN_FLIGHT);

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

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        VkDescriptorBufferInfo buffer_info{};
        buffer_info.buffer = vk_ctx->camera_buffer_alloc_mapped[i].buffer_alloc.buffer;
        buffer_info.offset = 0;
        buffer_info.range = sizeof(CameraUniformBuffer);

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
CameraCleanup(VulkanContext* vk_ctx)
{
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        BufferMappedDestroy(vk_ctx->allocator, &vk_ctx->camera_buffer_alloc_mapped[i]);
    }

    vkDestroyDescriptorSetLayout(vk_ctx->device, vk_ctx->camera_descriptor_set_layout, NULL);
}

// ~mgj: Asset Streaming

static AssetManager*
AssetManagerCreate(VkDevice device, U32 queue_family_index, async::Threads* threads,
                   U64 texture_map_size, U64 total_size_in_bytes)
{
    VulkanContext* vk_ctx = VulkanCtxGet();
    Arena* arena = ArenaAlloc();
    AssetManager* asset_store = PushStruct(arena, AssetManager);
    asset_store->arena = arena;
    asset_store->texture_hashmap = BufferAlloc<R_AssetItemList<Texture>>(arena, texture_map_size);
    asset_store->total_size = total_size_in_bytes;
    asset_store->work_queue = threads->msg_queue;
    asset_store->threaded_cmd_pools =
        BufferAlloc<AssetManagerCommandPool>(arena, threads->thread_handles.size);
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

    asset_store->cmd_wait_list = AssetManagerCmdListCreate();
    U32 cmd_queue_size = 10;
    asset_store->cmd_queue = async::QueueInit<CmdQueueItem>(
        arena, cmd_queue_size, vk_ctx->queue_family_indices.graphicsFamilyIndex);

    // ~mgj: textures
    asset_store->texture_arena = ArenaAlloc();
    // ~ mgj: buffers
    asset_store->buffer_arena = ArenaAlloc();
    return asset_store;
}
static void
AssetManagerDestroy(VulkanContext* vk_ctx, AssetManager* asset_store)
{
    for (U32 i = 0; i < asset_store->threaded_cmd_pools.size; i++)
    {
        vkDestroyCommandPool(vk_ctx->device, asset_store->threaded_cmd_pools.data[i].cmd_pool, 0);
        OS_MutexRelease(asset_store->threaded_cmd_pools.data[i].mutex);
    }
    async::QueueDestroy(asset_store->cmd_queue);
    AssetManagerCmdListDestroy(asset_store->cmd_wait_list);

    // ~mgj: textures
    ArenaRelease(asset_store->texture_arena);
    // ~ mgj: buffers
    ArenaRelease(asset_store->buffer_arena);

    ArenaRelease(asset_store->arena);
}

static void
AssetCmdQueueItemEnqueue(U32 thread_id, VkCommandBuffer cmd, R_ThreadInput* thread_input)
{
    AssetManager* asset_manager = VulkanCtxGet()->asset_manager;

    CmdQueueItem item = {.thread_input = thread_input, .thread_id = thread_id, .cmd_buffer = cmd};
    for (R_AssetLoadingInfoNode* asset_node = thread_input->asset_loading_wait_list.first;
         asset_node; asset_node = asset_node->next)
    {
        ins_atomic_u32_eval_cond_assign(&asset_node->load_info.is_loaded, 1, 0);
        if (asset_node->load_info.is_loaded)
        {
            DEBUG_LOG("Asset ID: %llu - Cmd Getting Queued\n", asset_node->load_info.info.id.id);
        }
    }
    async::QueuePush(asset_manager->cmd_queue, &item);
}

static void
AssetManagerExecuteCmds()
{
    VulkanContext* vk_ctx = VulkanCtxGet();
    AssetManager* asset_store = vk_ctx->asset_manager;

    for (U32 i = 0; i < asset_store->cmd_queue->queue_size; i++)
    {
        wrapper::CmdQueueItem item;
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

            for (R_AssetLoadingInfoNode* asset = item.thread_input->asset_loading_wait_list.first;
                 asset; asset = asset->next)
            {
                DEBUG_LOG("Asset ID: %llu - Submitted Command Buffer\n",
                          asset->load_info.info.id.id);
            }
            AssetManagerCmdListAdd(asset_store->cmd_wait_list, item);
        }
    }
}

force_inline static U64
HashIndexFromAssetId(R_AssetId id, U64 hashmap_size)
{
    return id.id % hashmap_size;
}

force_inline static B32
AssetIdCmp(R_AssetId a, R_AssetId b)
{
    return a.id == b.id;
}

static R_AssetItem<Texture>*
AssetManagerTextureItemGet(R_AssetId asset_id)
{
    ScratchScope scratch = ScratchScope(0, 0);
    VulkanContext* vk_ctx = VulkanCtxGet();
    AssetManager* asset_store = vk_ctx->asset_manager;

    R_AssetItemList<Texture>* texture_list =
        &asset_store->texture_hashmap
             .data[HashIndexFromAssetId(asset_id, asset_store->texture_hashmap.size)];

    return AssetManagerItemGet(asset_store->texture_arena, texture_list,
                               &asset_store->texture_free_list, asset_id);
}

static R_AssetItem<AssetItemBuffer>*
AssetManagerBufferItemGet(R_AssetId asset_id)
{
    VulkanContext* vk_ctx = VulkanCtxGet();
    AssetManager* asset_store = vk_ctx->asset_manager;

    return AssetManagerItemGet(asset_store->buffer_arena, &asset_store->buffer_list,
                               &asset_store->buffer_free_list, asset_id);
}

template <typename T>
static R_AssetItem<T>*
AssetManagerItemGet(Arena* arena, R_AssetItemList<T>* list, R_AssetItem<T>** free_list,
                    R_AssetId id)
{
    for (R_AssetItem<T>* buffer_result = list->first; buffer_result;
         buffer_result = buffer_result->next)
    {
        if (AssetIdCmp(buffer_result->id, id))
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
static R_AssetInfo
AssetInfoBufferCmd(VkCommandBuffer cmd, R_AssetId id, Buffer<T> buffer,
                   VkBufferUsageFlagBits usage_flags)
{
    VulkanContext* vk_ctx = VulkanCtxGet();
    U32 buffer_byte_size = buffer.size * sizeof(T);
    BufferAllocation buffer_alloc = StagingBufferCreate(vk_ctx->allocator, buffer_byte_size);

    BufferAllocation vertex_buffer_alloc =
        BufferUploadDevice(cmd, buffer_alloc, vk_ctx, buffer, usage_flags);

    R_AssetItem<AssetItemBuffer>* asset_vertex_buffer = AssetManagerBufferItemGet(id);
    asset_vertex_buffer->item.buffer_alloc = vertex_buffer_alloc;
    asset_vertex_buffer->item.staging_buffer = buffer_alloc;

    R_AssetInfo info = {.id = id, .type = R_AssetItemType_Buffer};

    return info;
}

static void
AssetManagerCmdDoneCheck()
{
    VulkanContext* vk_ctx = VulkanCtxGet();
    AssetManager* asset_store = vk_ctx->asset_manager;
    for (CmdQueueItem* cmd_queue_item = asset_store->cmd_wait_list->list_first; cmd_queue_item;)
    {
        CmdQueueItem* next = cmd_queue_item->next;
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
            for (R_AssetLoadingInfoNode* asset_node = thread_input->asset_loading_wait_list.first;
                 asset_node; asset_node = asset_node->next)
            {
                R_AssetLoadingInfo* asset_load_info = &asset_node->load_info;
                ins_atomic_u32_eval_cond_assign(&asset_node->load_info.is_loaded, 1, 0);
                if (asset_node->load_info.is_loaded)
                {
                    if (asset_load_info->info.type == R_AssetItemType_Texture)
                    {
                        R_AssetItem<Texture>* asset =
                            wrapper::AssetManagerTextureItemGet(asset_load_info->info.id);
                        BufferDestroy(vk_ctx->allocator, &asset->item.staging_buffer);
                        asset->item.pipeline_usage_type = asset_load_info->info.pipeline_usage_type;
                        Assert(asset_load_info->info.pipeline_usage_type !=
                               R_PipelineUsageType_Undefined);
                        if (asset->item.pipeline_usage_type == R_PipelineUsageType_3D)
                        {
                            VkDescriptorSet texture_handle = DescriptorSetCreate(
                                vk_ctx->arena, vk_ctx->device, vk_ctx->descriptor_pool,
                                vk_ctx->model_3D_pipeline.descriptor_set_layout, &asset->item);
                            asset->item.desc_set = texture_handle;
                        }
                        else if (asset_load_info->info.pipeline_usage_type ==
                                 R_PipelineUsageType_3DInstanced)
                        {
                            VkDescriptorSet texture_handle = DescriptorSetCreate(
                                vk_ctx->arena, vk_ctx->device, vk_ctx->descriptor_pool,
                                vk_ctx->model_3D_instance_pipeline.descriptor_set_layout,
                                &asset->item);
                            asset->item.desc_set = texture_handle;
                        }
                        asset->is_loaded = 1;
                    }
                    else if (asset_load_info->info.type == R_AssetItemType_Buffer)
                    {
                        R_AssetItem<AssetItemBuffer>* asset =
                            wrapper::AssetManagerBufferItemGet(asset_load_info->info.id);
                        BufferDestroy(vk_ctx->allocator, &asset->item.staging_buffer);
                        asset->is_loaded = 1;
                    }
                    Assert(asset_load_info->info.type != R_AssetItemType_Undefined);
                    DEBUG_LOG("Asset: %llu - Finished loading\n", asset_load_info->info.id.id);
                }
            }
            vkDestroyFence(vk_ctx->device, cmd_queue_item->fence, 0);
            ThreadInputDestroy(cmd_queue_item->thread_input);
            AssetManagerCmdListItemRemove(asset_store->cmd_wait_list, cmd_queue_item);
        }
        else if (result != VK_NOT_READY)
        {
            VK_CHECK_RESULT(result);
        }
        cmd_queue_item = next;
    }
}

static VkCommandBuffer
BeginCommand(VkDevice device, AssetManagerCommandPool threaded_cmd_pool)
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

static AssetManagerCmdList*
AssetManagerCmdListCreate()
{
    Arena* arena = ArenaAlloc();
    AssetManagerCmdList* cmd_list = PushStruct(arena, AssetManagerCmdList);
    cmd_list->arena = arena;
    return cmd_list;
}
static void
AssetManagerCmdListDestroy(AssetManagerCmdList* cmd_wait_list)
{
    while (cmd_wait_list->list_first)
    {
        AssetManagerCmdDoneCheck();
    }
    ArenaRelease(cmd_wait_list->arena);
}

static void
AssetManagerCmdListAdd(AssetManagerCmdList* cmd_list, CmdQueueItem item)
{
    CmdQueueItem* item_copy;
    if (cmd_list->free_list)
    {
        item_copy = cmd_list->free_list;
        SLLStackPop(cmd_list->free_list);
    }
    else
    {
        item_copy = PushStruct(cmd_list->arena, CmdQueueItem);
    }
    *item_copy = item;
    DLLPushBack(cmd_list->list_first, cmd_list->list_last, item_copy);
}
static void
AssetManagerCmdListItemRemove(AssetManagerCmdList* cmd_list, CmdQueueItem* item)
{
    DLLRemove(cmd_list->list_first, cmd_list->list_last, item);
    MemoryZeroStruct(item);
    SLLStackPush(cmd_list->free_list, item);
}

static void
AssetManagerBufferFree(R_AssetId asset_id)
{
    VulkanContext* vk_ctx = VulkanCtxGet();
    R_AssetItem<AssetItemBuffer>* item = AssetManagerBufferItemGet(asset_id);
    if (item->is_loaded)
    {
        BufferDestroy(vk_ctx->allocator, &item->item.buffer_alloc);
    }
    item->is_loaded = false;
}
static void
AssetManagerTextureFree(R_AssetId asset_id)
{
    VulkanContext* vk_ctx = VulkanCtxGet();
    R_AssetItem<Texture>* item = AssetManagerTextureItemGet(asset_id);
    if (item->is_loaded)
    {
        TextureDestroy(vk_ctx, &item->item);
    }
    item->is_loaded = false;
}

// ~mgj: Rendering

static void
DrawFrameReset()
{
    VulkanContext* vk_ctx = VulkanCtxGet();
    ArenaClear(vk_ctx->draw_frame_arena);
    vk_ctx->draw_frame = PushStruct(vk_ctx->draw_frame_arena, DrawFrame);
}

static void
PipelineDestroy(Pipeline* pipeline)
{
    VulkanContext* vk_ctx = VulkanCtxGet();
    vkDestroyPipeline(vk_ctx->device, pipeline->pipeline, NULL);
    vkDestroyPipelineLayout(vk_ctx->device, pipeline->pipeline_layout, NULL);
    vkDestroyDescriptorSetLayout(vk_ctx->device, pipeline->descriptor_set_layout, NULL);
}

static void
Model3DBucketAdd(BufferAllocation* vertex_buffer_allocation,
                 BufferAllocation* index_buffer_allocation, R_Handle texture_handle,
                 B32 depth_write_per_draw_call_only, U32 index_buffer_offset, U32 index_count)
{
    VulkanContext* vk_ctx = VulkanCtxGet();
    DrawFrame* draw_frame = vk_ctx->draw_frame;

    Model3DNode* node = PushStruct(vk_ctx->draw_frame_arena, Model3DNode);
    node->vertex_alloc = *vertex_buffer_allocation;
    node->index_alloc = *index_buffer_allocation;
    node->texture_handle = texture_handle;
    node->index_count = index_count;
    node->index_buffer_offset = index_buffer_offset;
    node->depth_write_per_draw_enabled = depth_write_per_draw_call_only;

    SLLQueuePush(draw_frame->model_3D_list.first, draw_frame->model_3D_list.last, node);
}

static void
Model3DInstanceBucketAdd(BufferAllocation* vertex_buffer_allocation,
                         BufferAllocation* index_buffer_allocation, R_Handle texture_handle,
                         R_BufferInfo* instance_buffer_info)
{
    U32 align = 16;
    VulkanContext* vk_ctx = VulkanCtxGet();
    DrawFrame* draw_frame = vk_ctx->draw_frame;
    Model3DInstance* instance_draw = &draw_frame->model_3D_instance_draw;

    Model3DInstanceNode* node = PushStruct(vk_ctx->draw_frame_arena, Model3DInstanceNode);
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
Model3DDraw(R_AssetInfo* vertex_info, R_AssetInfo* index_info, R_AssetInfo* texture_info,
            String8 texture_path, R_SamplerInfo* sampler_info, R_BufferInfo* vertex_buffer_info,
            R_BufferInfo* index_buffer_info, B32 depth_test_per_draw_call_only,
            U32 index_buffer_offset, U32 index_count)
{
    VulkanContext* vk_ctx = VulkanCtxGet();
    AssetManager* asset_manager = vk_ctx->asset_manager;

    R_AssetItem<AssetItemBuffer>* asset_vertex_buffer = AssetManagerBufferItemGet(vertex_info->id);
    R_AssetItem<AssetItemBuffer>* asset_index_buffer = AssetManagerBufferItemGet(index_info->id);
    R_AssetItem<Texture>* asset_texture = AssetManagerTextureItemGet(texture_info->id);

    if (asset_index_buffer->is_loaded && asset_index_buffer->is_loaded && asset_texture->is_loaded)
    {
        Model3DBucketAdd(&asset_vertex_buffer->item.buffer_alloc,
                         &asset_index_buffer->item.buffer_alloc, asset_texture->item.desc_set,
                         depth_test_per_draw_call_only, index_buffer_offset, index_count);
    }
    else if (!IsAssetLoadedOrInProgress(asset_vertex_buffer) ||
             !IsAssetLoadedOrInProgress(asset_index_buffer) ||
             !IsAssetLoadedOrInProgress(asset_texture))
    {
        R_ThreadInput* thread_input = ThreadInputCreate();
        R_AssetLoadingInfoNodeList* asset_loading_info = &thread_input->asset_loading_wait_list;

        if (!IsAssetLoadedOrInProgress(asset_vertex_buffer))
        {
            AssetBufferLoad(thread_input->arena, asset_vertex_buffer, asset_loading_info,
                            vertex_info, vertex_buffer_info);
        }
        if (!IsAssetLoadedOrInProgress(asset_index_buffer))
        {
            AssetBufferLoad(thread_input->arena, asset_index_buffer, asset_loading_info, index_info,
                            index_buffer_info);
        }
        if (!IsAssetLoadedOrInProgress(asset_texture))
        {
            AssetTextureLoad(thread_input->arena, asset_texture, asset_loading_info, texture_info,
                             sampler_info, texture_path);
        }
        Assert((asset_loading_info->first != NULL) && (asset_loading_info->last != NULL));
        async::QueueItem queue_input = {.data = thread_input, .worker_func = ThreadSetup};
        async::QueuePush(asset_manager->work_queue, &queue_input);
    }
}

static void
Model3DInstanceDraw(R_AssetInfo* vertex_info, R_AssetInfo* index_info, R_AssetInfo* texture_info,
                    String8 texture_path, R_SamplerInfo* sampler_info,
                    R_BufferInfo* vertex_buffer_info, R_BufferInfo* index_buffer_info,
                    R_BufferInfo* instance_buffer)
{
    VulkanContext* vk_ctx = VulkanCtxGet();
    AssetManager* asset_manager = vk_ctx->asset_manager;

    R_AssetItem<AssetItemBuffer>* asset_vertex_buffer = AssetManagerBufferItemGet(vertex_info->id);
    R_AssetItem<AssetItemBuffer>* asset_index_buffer = AssetManagerBufferItemGet(index_info->id);
    R_AssetItem<Texture>* asset_texture = AssetManagerTextureItemGet(texture_info->id);

    if (asset_index_buffer->is_loaded && asset_index_buffer->is_loaded && asset_texture->is_loaded)
    {
        Model3DInstanceBucketAdd(&asset_vertex_buffer->item.buffer_alloc,
                                 &asset_index_buffer->item.buffer_alloc,
                                 asset_texture->item.desc_set, instance_buffer);
    }
    else if (!IsAssetLoadedOrInProgress(asset_vertex_buffer) ||
             !IsAssetLoadedOrInProgress(asset_index_buffer) ||
             !IsAssetLoadedOrInProgress(asset_texture))
    {
        R_ThreadInput* thread_input = ThreadInputCreate();
        R_AssetLoadingInfoNodeList* asset_loading_info = &thread_input->asset_loading_wait_list;

        if (!IsAssetLoadedOrInProgress(asset_vertex_buffer))
        {
            AssetBufferLoad(thread_input->arena, asset_vertex_buffer, asset_loading_info,
                            vertex_info, vertex_buffer_info);
        }
        if (!IsAssetLoadedOrInProgress(asset_index_buffer))
        {
            AssetBufferLoad(thread_input->arena, asset_index_buffer, asset_loading_info, index_info,
                            index_buffer_info);
        }
        if (!IsAssetLoadedOrInProgress(asset_texture))
        {
            AssetTextureLoad(thread_input->arena, asset_texture, asset_loading_info, texture_info,
                             sampler_info, texture_path);
        }
        Assert((asset_loading_info->first != NULL) && (asset_loading_info->last != NULL));
        async::QueueItem queue_input = {.data = thread_input, .worker_func = ThreadSetup};
        async::QueuePush(asset_manager->work_queue, &queue_input);
    }
}

enum WriteType
{
    WriteType_Color,
    WriteType_Depth,
    WriteType_Count
};

static void
Model3DRendering()
{
    VulkanContext* vk_ctx = VulkanCtxGet();
    Pipeline* model_3D_pipeline = &vk_ctx->model_3D_pipeline;
    DrawFrame* draw_frame = vk_ctx->draw_frame;

    SwapchainResources* swapchain_resources = vk_ctx->swapchain_resources;
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
    for (Model3DNode* node = draw_frame->model_3D_list.first; node; node = node->next)
    {
        descriptor_sets[1] = (VkDescriptorSet)R_HandleToPtr(node->texture_handle);
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
                    vkCmdSetColorWriteEnableEXT(cmd_buffer, ArrayCount(color_write_enabled),
                                                color_write_enabled);
                }
                else if (write_type == WriteType_Depth)
                {
                    vkCmdSetDepthWriteEnable(cmd_buffer, VK_TRUE);
                    vkCmdSetColorWriteEnableEXT(cmd_buffer, ArrayCount(color_write_disabled),
                                                color_write_disabled);
                }

                vkCmdDrawIndexed(cmd_buffer, node->index_count, 1, node->index_buffer_offset, 0, 0);
            }
        }
        else
        {
            vkCmdSetDepthWriteEnable(cmd_buffer, VK_TRUE);
            vkCmdSetColorWriteEnableEXT(cmd_buffer, ArrayCount(color_write_enabled),
                                        color_write_enabled);
            vkCmdDrawIndexed(cmd_buffer, node->index_count, 1, node->index_buffer_offset, 0, 0);
        }
    }
}

} // namespace wrapper

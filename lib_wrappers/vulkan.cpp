

namespace wrapper
{
// ~mgj: global context
static VulkanContext* g_vk_ctx = 0;

// ~mgj: vulkan context
static void
VK_Cleanup(VulkanContext* vk_ctx)
{
    vkDeviceWaitIdle(vk_ctx->device);

    if (vk_ctx->enable_validation_layers)
    {
        DestroyDebugUtilsMessengerEXT(vk_ctx->instance, vk_ctx->debug_messenger, nullptr);
    }

    CameraCleanup(vk_ctx);

    VK_SwapChainCleanup(vk_ctx->device, vk_ctx->allocator, vk_ctx->swapchain_resources);

#ifdef TRACY_ENABLE
    for (U32 i = 0; i < ctx->profilingContext->tracyContexts.size; i++)
    {
        TracyVkDestroy(ctx->profilingContext->tracyContexts.data[i]);
    }
#endif
    vkDestroyCommandPool(vk_ctx->device, vk_ctx->command_pool, nullptr);

    vkDestroySurfaceKHR(vk_ctx->instance, vk_ctx->surface, nullptr);
    for (U32 i = 0; i < (U32)vk_ctx->MAX_FRAMES_IN_FLIGHT; i++)
    {
        vkDestroySemaphore(vk_ctx->device, vk_ctx->render_finished_semaphores.data[i], nullptr);
        vkDestroySemaphore(vk_ctx->device, vk_ctx->image_available_semaphores.data[i], nullptr);
        vkDestroyFence(vk_ctx->device, vk_ctx->in_flight_fences.data[i], nullptr);
    }

    vkDestroyDescriptorPool(vk_ctx->device, vk_ctx->descriptor_pool, 0);

    vmaDestroyAllocator(vk_ctx->allocator);

    AssetManagerDestroy(vk_ctx, vk_ctx->asset_store);

    vkDestroyDevice(vk_ctx->device, nullptr);
    vkDestroyInstance(vk_ctx->instance, nullptr);
    ArenaRelease(vk_ctx->arena);
}

static VulkanContext*
VK_VulkanInit(Context* ctx)
{
    IO* io_ctx = ctx->io;

    Temp scratch = ScratchBegin(0, 0);

    Arena* arena = ArenaAlloc();

    VulkanContext* vk_ctx = PushStruct(arena, VulkanContext);
    wrapper::VulkanCtxSet(vk_ctx);
    vk_ctx->arena = arena;
    vk_ctx->texture_path = Str8PathFromStr8List(arena, {ctx->cwd, S("textures")});
    vk_ctx->shader_path = Str8PathFromStr8List(arena, {ctx->cwd, S("shaders")});

    static const U32 MAX_FRAMES_IN_FLIGHT = 2;

    const char* validation_layers[] = {"VK_LAYER_KHRONOS_validation"};
    vk_ctx->validation_layers = BufferAlloc<String8>(vk_ctx->arena, ArrayCount(validation_layers));
    for (U32 i = 0; i < ArrayCount(validation_layers); i++)
    {
        vk_ctx->validation_layers.data[i] = {Str8CString(validation_layers[i])};
    }

    const char* device_extensions[2] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME,
                                        VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME};
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
    vk_ctx->asset_store =
        AssetManagerCreate(vk_ctx->device, vk_ctx->queue_family_indices.graphicsFamilyIndex,
                           ctx->thread_info, 1, GB(1));

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

// mgj: Road
static void
RoadDescriptorCreate(VkDescriptorPool desc_pool, RoadDescriptorCreateInfo* info, Texture* texture)
{
    info->w_road->descriptor_set_layout =
        RoadDescriptorSetLayoutCreate(info->vk_ctx->device, info->w_road);
    RoadDescriptorSetCreate(info->vk_ctx->device, desc_pool, texture, info->w_road,
                            info->vk_ctx->MAX_FRAMES_IN_FLIGHT);

    RoadPipelineCreate(info->w_road, info->vk_ctx->shader_path);
}

static void
RoadTextureCreate(U32 thread_id, AssetManagerCommandPool threaded_cmd_pool,
                  RoadTextureCreateInput* input, Texture* texture)
{
    ScratchScope scratch = ScratchScope(0, 0);
    VulkanContext* vk_ctx = input->vk_ctx;
    AssetManager* asset_store = vk_ctx->asset_store;
    Road* w_road = input->w_road;
    String8 texture_path = w_road->road_texture_path;

    S32 tex_width, tex_height, tex_channels;
    stbi_uc* pixels =
        stbi_load((char*)texture_path.str, &tex_width, &tex_height, &tex_channels, STBI_rgb_alpha);
    VkDeviceSize image_size = tex_width * tex_height * 4;
    U32 mip_level = (U32)(floor(log2(Max(tex_width, tex_height)))) + 1;

    if (!pixels)
    {
        exitWithError("TextureCreate: failed to load texture image!");
    }

    VmaAllocationCreateInfo vma_staging_info = {0};
    vma_staging_info.usage = VMA_MEMORY_USAGE_AUTO;
    vma_staging_info.flags =
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    vma_staging_info.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;

    texture->staging_buffer = BufferAllocationCreate(
        vk_ctx->allocator, image_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, vma_staging_info);

    vmaCopyMemoryToAllocation(vk_ctx->allocator, pixels, texture->staging_buffer.allocation, 0,
                              image_size);

    stbi_image_free(pixels);

    VmaAllocationCreateInfo vma_info = {.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};

    ImageAllocation image_alloc =
        ImageAllocationCreate(vk_ctx->allocator, tex_width, tex_height, VK_SAMPLE_COUNT_1_BIT,
                              VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL,
                              VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                  VK_IMAGE_USAGE_SAMPLED_BIT,
                              mip_level, vma_info);

    VkCommandBuffer command_buffer = BeginCommand(vk_ctx->device, threaded_cmd_pool);
    ImageLayoutTransition(command_buffer, image_alloc.image, VK_FORMAT_R8G8B8A8_SRGB,
                          VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          mip_level);
    ImageFromBufferCopy(command_buffer, texture->staging_buffer.buffer, image_alloc.image,
                        tex_width, tex_height);
    VK_GenerateMipmaps(command_buffer, image_alloc.image, tex_width, tex_height,
                       mip_level); // TODO: mip maps are usually not generated at
                                   // runtime.
    // They are usually stored in the texture file
    VK_CHECK_RESULT(vkEndCommandBuffer(command_buffer));

    ImageViewResource image_view_resource =
        ImageViewResourceCreate(vk_ctx->device, image_alloc.image, VK_FORMAT_R8G8B8A8_SRGB,
                                VK_IMAGE_ASPECT_COLOR_BIT, mip_level);

    VkSampler sampler =
        SamplerCreate(vk_ctx->device, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR, mip_level,
                      vk_ctx->physical_device_properties.limits.maxSamplerAnisotropy);

    ImageResource image_resource = {.image_alloc = image_alloc,
                                    .image_view_resource = image_view_resource};

    texture->image_resource = image_resource;
    texture->sampler = sampler;
    texture->width = tex_width;
    texture->height = tex_height;
    texture->mip_level_count = mip_level;

    CmdQueueItem cmd_input = {
        .asset_id = texture_path, .thread_id = thread_id, .cmd_buffer = command_buffer};
    async::QueuePush(asset_store->cmd_queue, cmd_input);
}

static void
RoadRenderPass(VulkanContext* vk_ctx, Road* w_road, city::Road* road, U32 image_index)
{
    VkExtent2D swap_chain_extent = vk_ctx->swapchain_resources->swapchain_extent;
    VkCommandBuffer command_buffer = vk_ctx->command_buffers.data[vk_ctx->current_frame];

    // Color attachment
    VkRenderingAttachmentInfo color_attachment{};
    color_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    color_attachment.imageView =
        vk_ctx->swapchain_resources->color_image_resource.image_view_resource.image_view;
    color_attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    // Depth attachment
    VkRenderingAttachmentInfo depth_attachment{};
    depth_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depth_attachment.imageView =
        vk_ctx->swapchain_resources->depth_image_resource.image_view_resource.image_view;
    depth_attachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    // Set up MSAA resolve if needed
    if (vk_ctx->msaa_samples != VK_SAMPLE_COUNT_1_BIT)
    {
        color_attachment.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
        color_attachment.resolveImageView =
            vk_ctx->swapchain_resources->image_resources.data[image_index]
                .image_view_resource.image_view;
        color_attachment.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }

    // Rendering info
    VkRenderingInfo rendering_info{};
    rendering_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering_info.renderArea.offset = {0, 0};
    rendering_info.renderArea.extent = vk_ctx->swapchain_resources->swapchain_extent;
    rendering_info.layerCount = 1;
    rendering_info.colorAttachmentCount = 1;
    rendering_info.pColorAttachments = &color_attachment;
    rendering_info.pDepthAttachment = &depth_attachment;

    vkCmdBeginRendering(command_buffer, &rendering_info);

    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, w_road->pipeline);

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

    VkBuffer vertex_buffers[] = {w_road->vertex_buffer.buffer_alloc.buffer};
    VkDeviceSize offsets[] = {0};

    RoadPushConstants road_push_constant = {.road_height = road->road_height,
                                            .texture_scale = road->texture_scale};
    vkCmdPushConstants(command_buffer, w_road->pipeline_layout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       sizeof(RoadPushConstants), &road_push_constant);
    VkDescriptorSet descriptor_sets[] = {vk_ctx->camera_descriptor_sets[vk_ctx->current_frame],
                                         w_road->descriptor_sets.data[vk_ctx->current_frame]};
    vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            w_road->pipeline_layout, 0, ArrayCount(descriptor_sets),
                            descriptor_sets, 0, NULL);
    vkCmdBindVertexBuffers(command_buffer, 0, 1, vertex_buffers, offsets);

    vkCmdDraw(command_buffer, w_road->vertex_buffer.size, 1, 0, 0);

    vkCmdEndRendering(command_buffer);
}

static VkDescriptorSetLayout
RoadDescriptorSetLayoutCreate(VkDevice device, Road* road)
{
    VkDescriptorSetLayoutBinding sampler_layout_binding{};
    sampler_layout_binding.binding = 0;
    sampler_layout_binding.descriptorCount = 1;
    sampler_layout_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    sampler_layout_binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding descriptor_layout_bindings[] = {sampler_layout_binding};

    return DescriptorSetLayoutCreate(device, descriptor_layout_bindings,
                                     ArrayCount(descriptor_layout_bindings));
}

static void
RoadDescriptorSetCreate(VkDevice device, VkDescriptorPool desc_pool, Texture* texture, Road* road,
                        U32 frames_in_flight)
{
    ScratchScope scratch = ScratchScope(0, 0);
    Buffer<VkDescriptorSetLayout> layouts =
        BufferAlloc<VkDescriptorSetLayout>(scratch.arena, frames_in_flight);

    for (U32 i = 0; i < layouts.size; i++)
    {
        layouts.data[i] = road->descriptor_set_layout;
    }

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = desc_pool;
    allocInfo.descriptorSetCount = layouts.size;
    allocInfo.pSetLayouts = layouts.data;

    Buffer<VkDescriptorSet> desc_sets = BufferAlloc<VkDescriptorSet>(road->arena, layouts.size);
    if (vkAllocateDescriptorSets(device, &allocInfo, desc_sets.data) != VK_SUCCESS)
    {
        exitWithError("RoadDescriptorSetCreate: failed to allocate descriptor sets!");
    }

    for (size_t i = 0; i < desc_sets.size; i++)
    {
        VkDescriptorImageInfo image_info{};
        image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        image_info.imageView = texture->image_resource.image_view_resource.image_view;
        image_info.sampler = texture->sampler;

        VkWriteDescriptorSet texture_sampler_desc{};
        texture_sampler_desc.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        texture_sampler_desc.dstSet = desc_sets.data[i];
        texture_sampler_desc.dstBinding = 0;
        texture_sampler_desc.dstArrayElement = 0;
        texture_sampler_desc.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        texture_sampler_desc.descriptorCount = 1;
        texture_sampler_desc.pImageInfo = &image_info;

        VkWriteDescriptorSet descriptors[] = {texture_sampler_desc};

        vkUpdateDescriptorSets(device, ArrayCount(descriptors), descriptors, 0, nullptr);
    }

    road->descriptor_sets = desc_sets;
}

static Road*
RoadCreate(VulkanContext* vk_ctx, city::Road* road)
{
    Arena* arena = ArenaAlloc();
    Road* w_road = PushStruct(arena, Road);
    w_road->arena = arena;
    road->texture_scale = 0.2f;

    w_road->descriptor_sets =
        BufferAlloc<VkDescriptorSet>(w_road->arena, vk_ctx->MAX_FRAMES_IN_FLIGHT);

    w_road->road_texture_path =
        Str8PathFromStr8List(road->arena, {vk_ctx->texture_path, S("road_texture.jpg")});

    w_road->texture_id = PushStr8Copy(arena, w_road->road_texture_path);
    AssetManagerItem* asset_item_state = AssetManagerTextureGetSlot(w_road->texture_id);

    AssetManagerRoadResourceLoadAsync(vk_ctx->asset_store, asset_item_state, vk_ctx,
                                      vk_ctx->shader_path, w_road, road);

    return w_road;
}

static void
RoadDestroy(city::Road* road, VulkanContext* vk_ctx)
{
    wrapper::Road* w_road = road->w_road;
    AssetManagerItem* texture_state = AssetManagerTextureGetSlot(w_road->texture_id);

    if (texture_state->is_loaded)
    {
        TextureDestroy(vk_ctx, &texture_state->asset);
    }
    BufferContextDestroy(vk_ctx->allocator, &w_road->vertex_buffer);
    vkDestroyDescriptorSetLayout(vk_ctx->device, w_road->descriptor_set_layout, 0);
    vkDestroyPipelineLayout(vk_ctx->device, w_road->pipeline_layout, nullptr);
    vkDestroyPipeline(vk_ctx->device, w_road->pipeline, nullptr);

    ArenaRelease(w_road->arena);
}

static void
RoadUpdate(city::Road* road, VulkanContext* vk_ctx, U32 image_index, String8 shader_path)
{
    wrapper::Road* w_road = road->w_road;
    AssetManagerItem* texture_state = AssetManagerTextureGetSlot(w_road->texture_id);

    if (texture_state->is_loaded && !w_road->descriptors_are_created)
    {
        RoadDescriptorCreateInfo desc_info = {.vk_ctx = vk_ctx, .road = road, .w_road = w_road};
        RoadDescriptorCreate(vk_ctx->descriptor_pool, &desc_info, &texture_state->asset);
        w_road->descriptors_are_created = 1;
    }

    if (texture_state->is_loaded && w_road->descriptors_are_created)
    {
        // TODO: this should be mapped to GPU memory (DEVICE_LOCAL)
        VkBufferFromBufferMapping<city::RoadVertex>(vk_ctx->allocator, &w_road->vertex_buffer,
                                                    road->vertex_buffer,
                                                    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        RoadRenderPass(vk_ctx, w_road, road, image_index);
    }
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
CarTextureCreate(String8 texture_id, CgltfSampler sampler)
{
    VulkanContext* vk_ctx = wrapper::VulkanCtxGet();
    AssetManager* asset_store = vk_ctx->asset_store;

    CarThreadInput* input = PushStruct(vk_ctx->arena, CarThreadInput);
    input->texture_id = texture_id;
    input->sampler = sampler;

    async::QueueItem item = {.data = input, .worker_func = CarTextureThreadSetup};
    async::QueuePush(asset_store->work_queue, item);
}

static void
CarTextureThreadSetup(async::ThreadInfo thread_info, void* input)
{
    CarThreadInput* input_cast = (CarThreadInput*)input;

    VulkanContext* vk_ctx = wrapper::VulkanCtxGet();
    AssetManager* asset_store = vk_ctx->asset_store;

    CarTextureCreateWorker(thread_info.thread_id,
                           asset_store->threaded_cmd_pools.data[thread_info.thread_id],
                           input_cast->texture_id, input_cast->sampler);
}

static void
CarTextureCreateWorker(U32 thread_id, AssetManagerCommandPool cmd_pool, String8 texture_id,
                       CgltfSampler sampler)
{
    // String8 texture_path = S("../../../textures/car_collection.ktx");
    VulkanContext* vk_ctx = wrapper::VulkanCtxGet();
    AssetManager* asset_store = vk_ctx->asset_store;
    AssetManagerItem* asset_store_texture = AssetManagerTextureGetSlot(texture_id);
    Texture* texture = &asset_store_texture->asset;

    VkSamplerCreateInfo sampler_create_info = wrapper::VkSamplerFromCgltfSampler(sampler);
    sampler_create_info.anisotropyEnable = VK_TRUE;
    sampler_create_info.maxAnisotropy = (F32)vk_ctx->msaa_samples;
    VkSampler vk_sampler = SamplerCreate(vk_ctx->device, &sampler_create_info);

    VkCommandBuffer cmd = BeginCommand(vk_ctx->device, cmd_pool); // Your helper

    // Get texture
    ktxTexture2* ktx_texture;
    ktx_error_code_e ktxresult = ktxTexture2_CreateFromNamedFile(
        (char*)texture_id.str, KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &ktx_texture);
    Assert(ktxresult == KTX_SUCCESS);

    BufferAllocation texture_staging_buffer = StagingBufferCreate(
        vk_ctx->allocator, ktx_texture->dataSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);

    ImageKtx2* image_ktx = ImageFromKtx2file(cmd, texture_staging_buffer, vk_ctx, ktx_texture);

    texture->image_resource = image_ktx->image_resource;
    texture->sampler = vk_sampler;
    texture->height = image_ktx->height;
    texture->width = image_ktx->width;
    texture->mip_level_count = image_ktx->mip_level_count;
    texture->staging_buffer = texture_staging_buffer;

    VK_CHECK_RESULT(vkEndCommandBuffer(cmd));

    CmdQueueItem item = {.asset_id = texture_id, .thread_id = thread_id, .cmd_buffer = cmd};
    async::QueuePush(asset_store->cmd_queue, item);
}

static void
CarPipelineCreate(Texture* texture, Car* car)
{
    VulkanContext* vk_ctx = VulkanCtxGet();
    VkDescriptorSetLayoutBinding desc_set_layout_info = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
    };
    VkDescriptorSetLayout desc_set_layout =
        DescriptorSetLayoutCreate(vk_ctx->device, &desc_set_layout_info, 1);
    Buffer<VkDescriptorSet> desc_sets =
        DescriptorSetCreate(car->arena, vk_ctx->device, vk_ctx->descriptor_pool, desc_set_layout,
                            texture, vk_ctx->MAX_FRAMES_IN_FLIGHT);

    car->pipeline_info = CarPipelineInfoCreate(vk_ctx, desc_set_layout);
    car->descriptor_set_layout = desc_set_layout;
    car->descriptor_sets = desc_sets;
}

static Car*
CarCreate(String8 texture_id, CgltfSampler sampler, Buffer<city::CarVertex> vertex_buffer,
          Buffer<U32> index_buffer)
{
    VulkanContext* vk_ctx = VulkanCtxGet();
    Arena* arena = ArenaAlloc();
    Car* car = PushStruct(arena, Car);
    car->arena = arena;
    car->texture_id = PushStr8Copy(arena, texture_id);

    // upload texture to GPU
    wrapper::CarTextureCreate(texture_id, sampler);

    // upload vertex and index to GPU
    VkCommandPoolCreateInfo cmd_pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
        .queueFamilyIndex = vk_ctx->queue_family_indices.graphicsFamilyIndex,
    };
    VkCommandPool cmd_pool;
    vkCreateCommandPool(vk_ctx->device, &cmd_pool_info, nullptr, &cmd_pool);

    VkCommandBuffer cmd = VK_BeginSingleTimeCommands(vk_ctx->device, cmd_pool); // Your helper

    U32 vertex_buffer_byte_size = vertex_buffer.size * sizeof(city::CarVertex);
    BufferAllocation vertex_staging_buffer = StagingBufferCreate(
        vk_ctx->allocator, vertex_buffer_byte_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    BufferAllocation index_staging_buffer = StagingBufferCreate(
        vk_ctx->allocator, vertex_buffer_byte_size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    BufferAllocation vertex_buffer_allocation = BufferUploadDevice(
        cmd, vertex_staging_buffer, vk_ctx, vertex_buffer, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    BufferAllocation index_buffer_allocation = BufferUploadDevice(
        cmd, index_staging_buffer, vk_ctx, index_buffer, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    VK_EndSingleTimeCommands(vk_ctx, cmd_pool, cmd);
    BufferDestroy(vk_ctx->allocator, vertex_staging_buffer);
    BufferDestroy(vk_ctx->allocator, index_staging_buffer);
    vkDestroyCommandPool(vk_ctx->device, cmd_pool, 0);

    car->index_buffer_alloc = index_buffer_allocation;
    car->vertex_buffer_alloc = vertex_buffer_allocation;

    return car;
}
static void
CarDestroy(VulkanContext* vk_ctx, Car* car)
{
    BufferDestroy(vk_ctx->allocator, car->vertex_buffer_alloc);
    BufferDestroy(vk_ctx->allocator, car->index_buffer_alloc);
    BufferContextDestroy(vk_ctx->allocator, &car->instance_buffer_mapped);
    vkDestroyDescriptorSetLayout(vk_ctx->device, car->descriptor_set_layout, 0);

    AssetManagerItem* asset_item = AssetManagerTextureGetSlot(car->texture_id);
    if (asset_item->is_loaded)
    {
        asset_item->is_loaded = 0;
        TextureDestroy(vk_ctx, &asset_item->asset);
    }
    PipelineInfoDestroy(vk_ctx->device, car->pipeline_info);

    ArenaRelease(car->arena);
}

static void
CarUpdate(city::CarSim* car_sim, Buffer<city::CarInstance> instance_buffer, U32 image_idx)
{
    VulkanContext* vk_ctx = VulkanCtxGet();
    Car* w_car = car_sim->car;
    AssetManagerItem* asset_item = AssetManagerTextureGetSlot(w_car->texture_id);

    if (!w_car->is_pipeline_created && asset_item->is_loaded)
    {
        CarPipelineCreate(&asset_item->asset, w_car);
        w_car->is_pipeline_created = 1;
    }
    if (w_car->is_pipeline_created)
    {
        VkBufferFromBufferMapping(vk_ctx->allocator, &w_car->instance_buffer_mapped,
                                  instance_buffer, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        CarRendering(vk_ctx, car_sim, image_idx, car_sim->cars.size);
    }
}
static PipelineInfo
CarPipelineInfoCreate(VulkanContext* vk_ctx, VkDescriptorSetLayout car_layout)
{
    ScratchScope scratch = ScratchScope(0, 0);

    String8 vert_path = CreatePathFromStrings(
        scratch.arena, Str8BufferFromCString(
                           scratch.arena, {(char*)vk_ctx->shader_path.str, "car", "car_vert.spv"}));
    String8 frag_path = CreatePathFromStrings(
        scratch.arena, Str8BufferFromCString(
                           scratch.arena, {(char*)vk_ctx->shader_path.str, "car", "car_frag.spv"}));

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
         .offset = offsetof(city::CarVertex, uv)},
        {.location = 2,
         .binding = 1,
         .format = VK_FORMAT_R32G32B32A32_SFLOAT,
         .offset = (U32)offsetof(city::CarInstance, x_basis)},
        {.location = 3,
         .binding = 1,
         .format = VK_FORMAT_R32G32B32A32_SFLOAT,
         .offset = (U32)offsetof(city::CarInstance, y_basis)},
        {.location = 4,
         .binding = 1,
         .format = VK_FORMAT_R32G32B32A32_SFLOAT,
         .offset = (U32)offsetof(city::CarInstance, z_basis)},
        {.location = 5,
         .binding = 1,
         .format = VK_FORMAT_R32G32B32A32_SFLOAT,
         .offset = (U32)offsetof(city::CarInstance, w_basis)},
    };
    VkVertexInputBindingDescription input_desc[] = {
        {.binding = 0, .stride = sizeof(city::CarVertex), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX},
        {.binding = 1,
         .stride = sizeof(city::CarInstance),
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

    VkPipelineColorBlendStateCreateInfo color_blending{};
    color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blending.attachmentCount = 1;
    color_blending.pAttachments = &color_blend_attachment;

    VkDescriptorSetLayout descriptor_set_layouts[] = {vk_ctx->camera_descriptor_set_layout,
                                                      car_layout};
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

    VkPipelineRenderingCreateInfo pipeline_rendering_info{};
    pipeline_rendering_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    pipeline_rendering_info.colorAttachmentCount = 1;
    pipeline_rendering_info.pColorAttachmentFormats = &vk_ctx->swapchain_resources->color_format;
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

    PipelineInfo pipeline_info = {.pipeline = pipeline, .pipeline_layout = pipeline_layout};
    return pipeline_info;
}

static void
CarRendering(VulkanContext* vk_ctx, city::CarSim* car, U32 image_idx, U32 instance_count)
{
    Car* w_car = car->car;
    SwapchainResources* swapchain_resources = vk_ctx->swapchain_resources;
    VkExtent2D swapchain_extent = swapchain_resources->swapchain_extent;
    VkImageView color_image_view =
        swapchain_resources->color_image_resource.image_view_resource.image_view;
    VkImageView depth_image_view =
        swapchain_resources->depth_image_resource.image_view_resource.image_view;
    VkImageView swapchain_image_view =
        swapchain_resources->image_resources.data[image_idx].image_view_resource.image_view;
    VkCommandBuffer cmd_buffer = vk_ctx->command_buffers.data[vk_ctx->current_frame];
    PipelineInfo pipeline_info = w_car->pipeline_info;
    U64 vertex_buffer_size = car->vertex_buffer.size;

    // Color attachment (assuming you want to render to the same targets)
    VkRenderingAttachmentInfo color_attachment{};
    color_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    color_attachment.imageView = color_image_view;
    color_attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; // Load existing content
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    // Depth attachment
    VkRenderingAttachmentInfo depth_attachment{};
    depth_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depth_attachment.imageView = depth_image_view;
    depth_attachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; // Load existing depth
    depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

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

    vkCmdBeginRendering(cmd_buffer, &rendering_info);

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

    vkCmdBindPipeline(cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_info.pipeline);

    VkBuffer vertex_buffers[] = {w_car->vertex_buffer_alloc.buffer,
                                 w_car->instance_buffer_mapped.buffer_alloc.buffer};
    VkDeviceSize offsets[] = {0, 0};

    VkDescriptorSet descriptor_sets[] = {vk_ctx->camera_descriptor_sets[vk_ctx->current_frame],
                                         w_car->descriptor_sets.data[vk_ctx->current_frame]};
    vkCmdBindDescriptorSets(cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipeline_info.pipeline_layout, 0, ArrayCount(descriptor_sets),
                            descriptor_sets, 0, NULL);
    vkCmdBindVertexBuffers(cmd_buffer, 0, 2, vertex_buffers, offsets);
    vkCmdBindIndexBuffer(cmd_buffer, w_car->index_buffer_alloc.buffer, 0, VK_INDEX_TYPE_UINT32);

    vkCmdDrawIndexed(cmd_buffer, vertex_buffer_size, instance_count, 0, 0, 0);

    vkCmdEndRendering(cmd_buffer);
}

static void
RoadPipelineCreate(Road* road, String8 shader_path)
{
    ScratchScope scratch = ScratchScope(0, 0);

    VulkanContext* vk_ctx = GlobalContextGet()->vk_ctx;

    String8 vert_path = CreatePathFromStrings(
        scratch.arena,
        Str8BufferFromCString(scratch.arena, {(char*)shader_path.str, "road", "road_vert.spv"}));
    String8 frag_path = CreatePathFromStrings(
        scratch.arena,
        Str8BufferFromCString(scratch.arena, {(char*)shader_path.str, "road", "road_frag.spv"}));

    ShaderModuleInfo vert_shader_stage_info =
        ShaderStageFromSpirv(scratch.arena, vk_ctx->device, VK_SHADER_STAGE_VERTEX_BIT, vert_path);
    ShaderModuleInfo frag_shader_stage_info = ShaderStageFromSpirv(
        scratch.arena, vk_ctx->device, VK_SHADER_STAGE_FRAGMENT_BIT, frag_path);

    VkPipelineShaderStageCreateInfo shaderStages[] = {vert_shader_stage_info.info,
                                                      frag_shader_stage_info.info};

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
        RoadAttributeDescriptionGet(scratch.arena);
    VkVertexInputBindingDescription input_desc = RoadBindingDescriptionGet();
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.vertexAttributeDescriptionCount = attr_desc.size;
    vertexInputInfo.pVertexBindingDescriptions = &input_desc;
    vertexInputInfo.pVertexAttributeDescriptions = attr_desc.data;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (F32)vk_ctx->swapchain_resources->swapchain_extent.width;
    viewport.height = (F32)vk_ctx->swapchain_resources->swapchain_extent.height;
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
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL; // TODO: helps in debugging, change to fill later
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace =
        VK_FRONT_FACE_COUNTER_CLOCKWISE; // TODO: might need to use counter-clockwise
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

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(RoadPushConstants);

    VkDescriptorSetLayout descriptor_set_layouts[2] = {vk_ctx->camera_descriptor_set_layout,
                                                       road->descriptor_set_layout};
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = ArrayCount(descriptor_set_layouts);
    pipelineLayoutInfo.pSetLayouts = descriptor_set_layouts;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

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

    if (vkCreatePipelineLayout(vk_ctx->device, &pipelineLayoutInfo, nullptr,
                               &road->pipeline_layout) != VK_SUCCESS)
    {
        exitWithError("failed to create pipeline layout!");
    }

    VkPipelineRenderingCreateInfo pipeline_rendering_info{};
    pipeline_rendering_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    pipeline_rendering_info.colorAttachmentCount = 1;
    pipeline_rendering_info.pColorAttachmentFormats = &vk_ctx->swapchain_resources->color_format;
    pipeline_rendering_info.depthAttachmentFormat = vk_ctx->swapchain_resources->depth_format;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.pNext = &pipeline_rendering_info;
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = ArrayCount(shaderStages);
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil; // Optional
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;

    pipelineInfo.layout = road->pipeline_layout;
    pipelineInfo.renderPass = VK_NULL_HANDLE;

    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE; // Optional
    pipelineInfo.basePipelineIndex = -1;              // Optional

    if (vkCreateGraphicsPipelines(vk_ctx->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr,
                                  &road->pipeline) != VK_SUCCESS)
    {
        exitWithError("failed to create graphics pipeline!");
    }

    return;
}

static VkVertexInputBindingDescription
RoadBindingDescriptionGet()
{
    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(city::RoadVertex);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    return bindingDescription;
}

static Buffer<VkVertexInputAttributeDescription>
RoadAttributeDescriptionGet(Arena* arena)
{
    Buffer<VkVertexInputAttributeDescription> attribute_descriptions =
        BufferAlloc<VkVertexInputAttributeDescription>(arena, 1);
    attribute_descriptions.data[0].binding = 0;
    attribute_descriptions.data[0].location = 0;
    attribute_descriptions.data[0].format = VK_FORMAT_R32G32_SFLOAT;
    attribute_descriptions.data[0].offset = offsetof(city::RoadVertex, pos);

    return attribute_descriptions;
}

// ~mgj: Camera functions

static void
CameraUniformBufferCreate(VulkanContext* vk_ctx)
{
    VkDeviceSize camera_buffer_size = sizeof(CameraUniformBuffer);
    VkBufferUsageFlags buffer_usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

    for (U32 i = 0; i < vk_ctx->MAX_FRAMES_IN_FLIGHT; i++)
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
    U32 max_frames_in_flight = VulkanContext::MAX_FRAMES_IN_FLIGHT;

    Buffer<VkDescriptorSetLayout> layouts =
        BufferAlloc<VkDescriptorSetLayout>(arena, max_frames_in_flight);

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

    for (size_t i = 0; i < max_frames_in_flight; i++)
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
    for (size_t i = 0; i < VulkanContext::MAX_FRAMES_IN_FLIGHT; i++)
    {
        BufferMappedDestroy(vk_ctx->allocator, vk_ctx->camera_buffer_alloc_mapped[i]);
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
    asset_store->hashmap = BufferAlloc<AssetManagerItemStateList>(arena, texture_map_size);
    asset_store->total_size = total_size_in_bytes;
    asset_store->work_queue = threads->msg_queue;
    asset_store->threaded_cmd_pools =
        BufferAlloc<AssetManagerCommandPool>(arena, threads->thread_handles.size);
    asset_store->free_list = nullptr;

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
    AssetManagerExecuteCmds(); // finish executing commands already in the queue
    async::QueueDestroy(asset_store->cmd_queue);
    AssetManagerCmdListDestroy(asset_store->cmd_wait_list);
    ArenaRelease(asset_store->arena);
}

static void
AssetManagerExecuteCmds()
{
    VulkanContext* vk_ctx = VulkanCtxGet();
    AssetManager* asset_store = vk_ctx->asset_store;

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
            vkCreateFence(vk_ctx->device, &fence_info, nullptr, &item.fence);

            vkQueueSubmit(vk_ctx->graphics_queue, 1, &submit_info, item.fence);

            AssetManagerCmdListAdd(asset_store->cmd_wait_list, item);
        }
    }
}

static AssetManagerItem*
AssetManagerTextureGetSlot(String8 asset_id)
{
    ScratchScope scratch = ScratchScope(0, 0);
    VulkanContext* vk_ctx = VulkanCtxGet();
    AssetManager* asset_store = vk_ctx->asset_store;

    String8 asset_id_copy = PushStr8Copy(scratch.arena, asset_id);
    U64 asset_idx = AssetManagerHash(asset_id_copy);
    AssetManagerItemStateList* texture_list =
        &asset_store->hashmap.data[asset_idx % asset_store->hashmap.size];
    for (AssetManagerItem* texture_result = texture_list->first; texture_result;
         texture_result = texture_result->next)
    {
        if (Str8Cmp(texture_result->id, asset_id_copy))
        {
            return texture_result;
        }
    }
    AssetManagerItem* asset_item = {0};
    if (asset_store->free_list)
    {
        asset_item = asset_store->free_list;
        SLLStackPop(asset_item);
    }
    else
    {
        asset_item = PushStruct(asset_store->arena, AssetManagerItem);
        asset_item->id = PushStr8Copy(asset_store->arena, asset_id_copy);
        SLLQueuePushFront(texture_list->first, texture_list->last, asset_item);
    }
    return asset_item;
}

static void
AssetManagerCmdDoneCheck()
{
    VulkanContext* vk_ctx = VulkanCtxGet();
    AssetManager* asset_store = vk_ctx->asset_store;
    for (CmdQueueItem* cmd_queue_item = asset_store->cmd_wait_list->list_first; cmd_queue_item;
         cmd_queue_item = cmd_queue_item->next)
    {
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
            wrapper::AssetManagerItem* asset =
                wrapper::AssetManagerTextureGetSlot(cmd_queue_item->asset_id);
            BufferDestroy(vk_ctx->allocator, asset->asset.staging_buffer);
            asset->is_loaded = 1;
            vkDestroyFence(vk_ctx->device, cmd_queue_item->fence, 0);
            AssetManagerCmdListRemove(asset_store->cmd_wait_list, cmd_queue_item);
        }
        else if (result != VK_NOT_READY)
        {
            VK_CHECK_RESULT(result);
        }
    }
}

static void
AssetManagerRoadResourceLoadAsync(AssetManager* asset_store, AssetManagerItem* texture,
                                  VulkanContext* vk_ctx, String8 shader_path, Road* w_road,
                                  city::Road* road)
{
    RoadTextureCreateInput* input = PushStruct(asset_store->arena, RoadTextureCreateInput);
    input->vk_ctx = vk_ctx;
    input->w_road = w_road;

    RoadThreadInput* thread_input = PushStruct(asset_store->arena, RoadThreadInput);
    thread_input->asset_store_texture = texture;
    thread_input->texture_input = input;
    thread_input->texture_func = RoadTextureCreate;
    thread_input->threaded_cmd_pools = asset_store->threaded_cmd_pools;

    async::QueueItem queue_input = {.data = thread_input,
                                    .worker_func = AssetManagerTextureThreadMain};
    async::QueuePush(asset_store->work_queue, queue_input);
}

static void
AssetManagerTextureThreadMain(async::ThreadInfo thread_info, void* data)
{
    RoadThreadInput* input = (RoadThreadInput*)data;
    AssetManagerItem* item = input->asset_store_texture;
    item->is_loaded = 0;
    input->texture_func(thread_info.thread_id,
                        input->threaded_cmd_pools.data[thread_info.thread_id], input->texture_input,
                        &item->asset);
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
AssetManagerCmdListRemove(AssetManagerCmdList* cmd_list, CmdQueueItem* item)
{
    DLLRemove(cmd_list->list_first, cmd_list->list_last, item);
    MemoryZeroStruct(item);
    SLLStackPush(cmd_list->free_list, item);
}
static U64
AssetManagerHash(String8 str)
{
    return HashU128FromStr8(str).u64[0];
}

} // namespace wrapper

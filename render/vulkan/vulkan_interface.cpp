static R_Handle
R_Tex2dAlloc(R_ResourceKind kind, Vec2S32 size, R_Tex2DFormat format, void* data)
{
    (void)kind; // ignoreing kind at the moment
    U32 mip_levels = 1;
    wrapper::VulkanContext* vk_ctx = wrapper::VulkanCtxGet();
    wrapper::AssetManager* asset_manager = vk_ctx->asset_manager;

    // ~mgj Create Vulkan Image
    VkFormat vk_format =
        R_VkFormatFromTex2DFormat(format); // is format function correct? use UNORM instead of SRGB
    VmaAllocationCreateInfo vma_info = {.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
    wrapper::ImageAllocation image_alloc = wrapper::ImageAllocationCreate(
        vk_ctx->allocator, (U32)size.x, (U32)size.y, VK_SAMPLE_COUNT_1_BIT, vk_format,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
            VK_IMAGE_USAGE_SAMPLED_BIT,
        mip_levels, vma_info);

    wrapper::ImageViewResource image_view_resource = wrapper::ImageViewResourceCreate(
        vk_ctx->device, image_alloc.image, vk_format, VK_IMAGE_ASPECT_COLOR_BIT, mip_levels);
    wrapper::ImageResource image_resource = {.image_alloc = image_alloc,
                                             .image_view_resource = image_view_resource};

    // ~mgj: Create Vulkan Sampler object
    R_SamplerInfo sampler_info = {}; // TODO: Are the default values correct?
    VkSamplerCreateInfo sampler_create_info = {};
    wrapper::VkSamplerCreateInfoFromSamplerInfo(&sampler_info, &sampler_create_info);
    sampler_create_info.anisotropyEnable = VK_TRUE;
    sampler_create_info.maxAnisotropy = (F32)vk_ctx->msaa_samples;
    VkSampler vk_sampler = wrapper::SamplerCreate(vk_ctx->device, &sampler_create_info);

    // ~mgj: Create Texture Handle
    VkDescriptorSet desc_set = wrapper::VK_DescriptorSetCreate(
        vk_ctx->arena, vk_ctx->device, vk_ctx->descriptor_pool, vk_ctx->font_descriptor_set_layout,
        image_view_resource.image_view, vk_sampler);

    // ~mgj: save texture as asset
    VK_Tex2D* vk_tex2d = r_tex2d_free_list;
    if (vk_tex2d)
    {
        SLLStackPop(vk_tex2d);
    }
    else
    {
        vk_tex2d = PushStruct(asset_manager->arena, VK_Tex2D);
        SLLQueuePushFront(r_tex2d_list.first, r_tex2d_list.last, vk_tex2d);
    }
    vk_tex2d->format = format;
    vk_tex2d->desc_set = desc_set;
    vk_tex2d->image_resource = image_resource;
    vk_tex2d->sampler = vk_sampler;

    return VK_Tex2DToHandle(vk_tex2d);
}

static void
R_Tex2dRelease(R_Handle texture_handle)
{
    wrapper::VulkanContext* vk_ctx = wrapper::VulkanCtxGet();
    VK_Tex2D* vk_tex2d = VK_Tex2DFromHandle(texture_handle);
    vkDestroySampler(vk_ctx->device, vk_tex2d->sampler, NULL);
    wrapper::ImageResourceDestroy(vk_ctx->allocator, vk_tex2d->image_resource);
}

static void
R_FillTex2dRegion(R_Handle handle, Rng2S32 subrect, void* data)
{
    wrapper::VulkanContext* vk_ctx = wrapper::VulkanCtxGet();
    VK_Tex2D* vk_texture = VK_Tex2DFromHandle(handle);

    U8 num_bytes_per_pixel = r_tex2d_format_bytes_per_pixel_table[vk_texture->format];

    // ~mgj: Create and transfer data to staging buffer
    Vec2S32 image_size = Sub_2S32(subrect.p1, subrect.p0);
    U32 buffer_size = num_bytes_per_pixel * image_size.x * image_size.y;
    wrapper::BufferAllocation texture_staging_buffer =
        wrapper::StagingBufferCreate(vk_ctx->allocator, buffer_size);
    vmaCopyMemoryToAllocation(vk_ctx->allocator, data, texture_staging_buffer.allocation, 0,
                              buffer_size);

    // ~mgj: Copy data from staging buffer to texture
    wrapper::ImageResource* vk_image_resource = &vk_texture->image_resource;
    wrapper::ImageAllocation* image_allocation = &vk_image_resource->image_alloc;
    VkImage vk_image = image_allocation->image;
    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = vk_image,
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                             .baseMipLevel = 0,
                             .levelCount = 1,
                             .baseArrayLayer = 0,
                             .layerCount = 1},
    };

    VkCommandBufferAllocateInfo cmd_alloc_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = vk_ctx->command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkCommandBuffer cmd;
    VK_CHECK_RESULT(vkAllocateCommandBuffers(vk_ctx->device, &cmd_alloc_info, &cmd));

    VkBufferImageCopy region = {
        .bufferOffset = 0,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                             .mipLevel = 0,
                             .baseArrayLayer = 0,
                             .layerCount = 1},
        .imageOffset = {subrect.p0.x, subrect.p0.y, 0},
        .imageExtent = {(U32)image_size.x, (U32)image_size.y, 1},
    };

    vkCmdCopyBufferToImage(cmd, texture_staging_buffer.buffer, vk_image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    VkImageMemoryBarrier2 imageBarrier = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                                          .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
                                          .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                          .dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                          .dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                                          .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                          .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                          .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                          .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                          .image = vk_image,
                                          .subresourceRange = {
                                              .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                              .baseMipLevel = 0,
                                              .levelCount = 1,
                                              .baseArrayLayer = 0,
                                              .layerCount = 1,
                                          }};

    VkDependencyInfo depInfo = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                .imageMemoryBarrierCount = 1,
                                .pImageMemoryBarriers = &imageBarrier};

    vkCmdPipelineBarrier2(cmd, &depInfo);

    VK_CHECK_RESULT(vkEndCommandBuffer(cmd));

    VkCommandBufferSubmitInfo cmd_info = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
                                          .commandBuffer = cmd};
    VkSubmitInfo2 submit_info = {
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &cmd_info,
    };

    vkQueueSubmit2(vk_ctx->graphics_queue, 1, &submit_info, NULL);

    wrapper::BufferDestroy(vk_ctx->allocator, &texture_staging_buffer);
}

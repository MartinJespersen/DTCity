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

inline static Context*
ctx_get()
{
    Assert(g_vk_ctx);
    return g_vk_ctx;
}

static void
texture_destroy(Texture* texture)
{
    Context* vk_ctx = ctx_get();
    if (texture->staging_buffer.buffer != 0)
    {
        buffer_destroy(vk_ctx->allocator, &texture->staging_buffer);
    }
    image_resource_destroy(vk_ctx->allocator, texture->image_resource);
    vkDestroySampler(vk_ctx->device, texture->sampler, nullptr);
}

g_internal bool
ktx2_check(U8* buf, U64 size)
{
    bool result = false;
    const unsigned char ktx2_magic[12] = {0xab, 0x4b, 0x54, 0x58, 0x20, 0x32,
                                          0x30, 0xbb, 0x0d, 0x0a, 0x1a, 0x0a};
    if (size >= ArrayCount(ktx2_magic) && MemoryMatch(ktx2_magic, buf, ArrayCount(ktx2_magic)))
    {
        result = true;
    }
    return result;
}

g_internal void
texture_ktx_cmd_record(VkCommandBuffer cmd, Texture* tex, ::Buffer<U8> tex_buf)
{
    ScratchScope scratch = ScratchScope(0, 0);
    Context* vk_ctx = ctx_get();

    ktxTexture2* ktx_texture;
    ktx_error_code_e ktxresult =
        ktxTexture2_CreateFromMemory(tex_buf.data, tex_buf.size, NULL, &ktx_texture);

    BufferAllocation staging = {};

    if (ktxresult == KTX_SUCCESS)
    {
        U32 mip_levels = ktx_texture->numLevels;
        U32 base_width = ktx_texture->baseWidth;
        U32 base_height = ktx_texture->baseHeight;
        U32 base_depth = ktx_texture->baseDepth;

        ::Buffer<U32> mip_level_offsets = BufferAlloc<U32>(scratch.arena, mip_levels);
        for (U32 i = 0; i < ktx_texture->numLevels; ++i)
        {
            ktx_size_t offset;
            KTX_error_code ktx_error =
                ktxTexture_GetImageOffset((ktxTexture*)ktx_texture, i, 0, 0, &offset);
            if (ktx_error != KTX_SUCCESS)
            {
                exit_with_error("Failed to get image offset for mipmap level %u", i);
            }
            mip_level_offsets.data[i] = offset;
        }

        VkFormat vk_format = ktxTexture2_GetVkFormat(ktx_texture);
        {
            VkFormatProperties vk_format_properties;
            vkGetPhysicalDeviceFormatProperties(vk_ctx->physical_device, vk_format,
                                                &vk_format_properties);
            if ((vk_format_properties.optimalTilingFeatures &
                 VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) == 0)
            {
                exit_with_error("Unsupported image tiling features for format %s", vk_format);
            }
        }

        VkBufferCreateInfo staging_buf_create_info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        staging_buf_create_info.size = ktx_texture->dataSize;
        staging_buf_create_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

        VmaAllocationCreateInfo staging_alloc_create_info = {};
        staging_alloc_create_info.usage = VMA_MEMORY_USAGE_AUTO;
        staging_alloc_create_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                                          VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VkBuffer staging_buf;
        VmaAllocation staging_alloc;
        VmaAllocationInfo staging_alloc_info;
        vmaCreateBuffer(vk_ctx->allocator, &staging_buf_create_info, &staging_alloc_create_info,
                        &staging_buf, &staging_alloc, &staging_alloc_info);

        VmaAllocationCreateInfo vma_info = {.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
        ImageAllocation image_alloc = image_allocation_create(
            vk_ctx->allocator, base_width, base_height, VK_SAMPLE_COUNT_1_BIT, vk_format,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                VK_IMAGE_USAGE_SAMPLED_BIT,
            mip_levels, vma_info);

        ImageViewResource image_view_resource =
            image_view_resource_create(vk_ctx->device, image_alloc.image, VK_FORMAT_R8G8B8A8_SRGB,
                                       VK_IMAGE_ASPECT_COLOR_BIT, mip_levels);

        KTX_error_code ktx_error = ktxTexture_LoadImageData(
            (ktxTexture*)ktx_texture, (U8*)staging_alloc_info.pMappedData, ktx_texture->dataSize);
        if (ktx_error != KTX_SUCCESS)
        {
            exit_with_error("Failed to load KTX texture data");
        }

        VkBufferImageCopy* regions = PushArray(scratch.arena, VkBufferImageCopy, mip_levels);

        for (uint32_t level = 0; level < mip_levels; ++level)
        {
            uint32_t mip_width = Max(base_width >> level, 1);
            uint32_t mip_height = Max(base_height >> level, 1);
            uint32_t mip_depth = Max(base_depth >> level, 1);

            regions[level] = {.bufferOffset = mip_level_offsets.data[level],
                              .bufferRowLength = 0,
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
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, NULL, 0, NULL, 1, &barrier);
        vkCmdCopyBufferToImage(cmd, staging_buf, image_alloc.image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mip_levels, regions);

        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, 1,
                             &barrier);

        tex->image_resource = {.image_alloc = image_alloc,
                               .image_view_resource = image_view_resource};
        tex->staging_buffer = {
            .buffer = staging_buf, .allocation = staging_alloc, .size = ktx_texture->dataSize};

        ktxTexture_Destroy((ktxTexture*)ktx_texture);
    }
    Assert(ktxresult == KTX_SUCCESS);
}

g_internal void
blit_transition_image(VkCommandBuffer cmd_buf, VkImage image, VkImageLayout src_layout,
                      VkImageLayout dst_layout, U32 mip_level)
{
    // ~mgj: Transition color attachment images for presentation or transfer
    VkImageMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
    barrier.oldLayout = src_layout;
    barrier.newLayout = dst_layout;
    barrier.image = image;
    barrier.subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                .baseMipLevel = mip_level,
                                .levelCount = 1,
                                .baseArrayLayer = 0,
                                .layerCount = 1};

    VkImageMemoryBarrier2 barriers[] = {barrier};
    VkDependencyInfo layout_transition_info = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                               .imageMemoryBarrierCount = ArrayCount(barriers),
                                               .pImageMemoryBarriers = barriers};

    vkCmdPipelineBarrier2(cmd_buf, &layout_transition_info);
}

g_internal void
colormap_texture_cmd_record(VkCommandBuffer cmd, Texture* tex, Buffer<U8> buf)
{
    ScratchScope scratch = ScratchScope(0, 0);
    Context* vk_ctx = ctx_get();
    U32 sizeof_rgb = 3;
    U32 sizeof_f32 = sizeof(F32);
    U32 colormap_size = buf.size / sizeof_rgb / sizeof_f32;
    U32 staging_buffer_size = colormap_size * 4 * sizeof(F32);
    AssertAlways(colormap_size == 256);
    VkFormat vk_format = VK_FORMAT_R32G32B32A32_SFLOAT;
    {
        VkFormatProperties vk_format_properties;
        vkGetPhysicalDeviceFormatProperties(vk_ctx->physical_device, vk_format,
                                            &vk_format_properties);
        if ((vk_format_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) == 0)
        {
            exit_with_error("Unsupported image tiling features for format %s", vk_format);
        }
    }

    VkBufferCreateInfo staging_buf_create_info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    staging_buf_create_info.size = staging_buffer_size;
    staging_buf_create_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo staging_alloc_create_info = {};
    staging_alloc_create_info.usage = VMA_MEMORY_USAGE_AUTO;
    staging_alloc_create_info.flags =
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VkBuffer staging_buf;
    VmaAllocation staging_alloc;
    VmaAllocationInfo staging_alloc_info;
    vmaCreateBuffer(vk_ctx->allocator, &staging_buf_create_info, &staging_alloc_create_info,
                    &staging_buf, &staging_alloc, &staging_alloc_info);

    // Convert RGB to RGBA by adding alpha channel (1.0)
    F32* src = (F32*)buf.data;
    F32* dst = (F32*)staging_alloc_info.pMappedData;
    for (U32 i = 0; i < colormap_size; i++)
    {
        dst[i * 4 + 0] = src[i * 3 + 0]; // R
        dst[i * 4 + 1] = src[i * 3 + 1]; // G
        dst[i * 4 + 2] = src[i * 3 + 2]; // B
        dst[i * 4 + 3] = 1.0f;           // A
    }

    U32 mip_levels = 1;
    VmaAllocationCreateInfo vma_info = {.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
    ImageAllocation image_alloc =
        image_allocation_create(vk_ctx->allocator, colormap_size, 1, VK_SAMPLE_COUNT_1_BIT,
                                vk_format, VK_IMAGE_TILING_OPTIMAL,
                                VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                    VK_IMAGE_USAGE_SAMPLED_BIT,
                                mip_levels, vma_info, VK_IMAGE_TYPE_1D);

    ImageViewResource image_view_resource =
        image_view_resource_create(vk_ctx->device, image_alloc.image, vk_format,
                                   VK_IMAGE_ASPECT_COLOR_BIT, mip_levels, VK_IMAGE_VIEW_TYPE_1D);

    VkBufferImageCopy* region = PushStruct(scratch.arena, VkBufferImageCopy);
    *region = {.bufferOffset = 0,
               .bufferRowLength = 0,
               .bufferImageHeight = 0,
               .imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                    .mipLevel = 0,
                                    .baseArrayLayer = 0,
                                    .layerCount = 1},
               .imageOffset = {0, 0, 0},
               .imageExtent = {colormap_size, 1, 1}};

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
    vkCmdCopyBufferToImage(cmd, staging_buf, image_alloc.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mip_levels, region);

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, NULL, 0, NULL, 1, &barrier);

    tex->image_resource = {.image_alloc = image_alloc, .image_view_resource = image_view_resource};
    tex->staging_buffer = {
        .buffer = staging_buf, .allocation = staging_alloc, .size = staging_buffer_size};
}

g_internal B32
texture_cmd_record(VkCommandBuffer cmd, Texture* tex, Buffer<U8> tex_buf)
{
    Context* vk_ctx = ctx_get();
    S32 width;
    S32 height;
    S32 desired_num_channels = STBI_rgb_alpha;
    U8* data;
    U8* image_data = stbi_load_from_memory(tex_buf.data, tex_buf.size, &width, &height, NULL,
                                           desired_num_channels);
    B32 err = false;
    err = !image_data;

    Assert(image_data);
    if (image_data)
    {
        U64 total_size = (U64)width * (U64)height * (U64)desired_num_channels;
        U32 mip_levels = floor(log2f(Max(width, height))) + 1;
        VkFormat vk_format = VK_FORMAT_R8G8B8A8_SRGB;

        // upload base mip level
        VkBufferCreateInfo staging_buf_create_info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        staging_buf_create_info.size = total_size;
        staging_buf_create_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

        VmaAllocationCreateInfo staging_alloc_create_info = {};
        staging_alloc_create_info.usage = VMA_MEMORY_USAGE_AUTO;
        staging_alloc_create_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                                          VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VkBuffer staging_buf;
        VmaAllocation staging_alloc;
        VmaAllocationInfo staging_alloc_info;
        err |=
            vmaCreateBuffer(vk_ctx->allocator, &staging_buf_create_info, &staging_alloc_create_info,
                            &staging_buf, &staging_alloc, &staging_alloc_info);
        if (err)
        {
            return err;
        }

        VmaAllocationCreateInfo alloc_create_info = {
            .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT};
        VmaAllocationCreateInfo vma_info = {.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
        ImageAllocation image_alloc = image_allocation_create(
            vk_ctx->allocator, width, height, VK_SAMPLE_COUNT_1_BIT, vk_format,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                VK_IMAGE_USAGE_SAMPLED_BIT,
            mip_levels, vma_info);

        err |=
            vmaCopyMemoryToAllocation(vk_ctx->allocator, image_data, staging_alloc, 0, total_size);
        if (err)
        {
            return err;
        }

        ImageViewResource image_view_resource = image_view_resource_create(
            vk_ctx->device, image_alloc.image, vk_format, VK_IMAGE_ASPECT_COLOR_BIT, mip_levels);

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
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, NULL, 0, NULL, 1, &barrier);

        VkBufferImageCopy region;
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                   .mipLevel = 0,
                                   .baseArrayLayer = 0,
                                   .layerCount = 1};
        region.imageOffset = {.x = 0, .y = 0, .z = 0};
        region.imageExtent = {.width = (U32)width, .height = (U32)height, .depth = 1};

        vkCmdCopyBufferToImage(cmd, staging_buf, image_alloc.image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        S32 dst_img_width = width;
        S32 dst_img_height = height;
        for (U32 blit_lvl = 1; blit_lvl < mip_levels; blit_lvl++)
        {
            S32 src_img_width = dst_img_width;
            S32 src_img_height = dst_img_height;
            dst_img_width = Max((src_img_width >> 1), 1);
            dst_img_height = Max((src_img_height >> 1), 1);

            blit_transition_image(cmd, image_alloc.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, blit_lvl - 1);

            VkImageBlit2 image_blit = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2,
                .srcSubresource =
                    {
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .mipLevel = blit_lvl - 1,
                        .baseArrayLayer = 0,
                        .layerCount = 1,
                    },
                .srcOffsets =
                    {
                        {0, 0, 0},
                        {src_img_width, src_img_height, 1},
                    },
                .dstSubresource =
                    {
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .mipLevel = blit_lvl,
                        .baseArrayLayer = 0,
                        .layerCount = 1,
                    },
                .dstOffsets =
                    {
                        {0, 0, 0},
                        {dst_img_width, dst_img_height, 1},
                    },
            };

            VkBlitImageInfo2 blit_info = {
                .sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2,
                .srcImage = image_alloc.image,
                .srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                .dstImage = image_alloc.image,
                .dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .regionCount = 1,
                .pRegions = &image_blit,
                .filter = VK_FILTER_NEAREST,
            };
            vkCmdBlitImage2(cmd, &blit_info);
            blit_transition_image(cmd, image_alloc.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, blit_lvl - 1);
        }
        blit_transition_image(cmd, image_alloc.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, mip_levels - 1);

        stbi_image_free(image_data);
        tex->image_resource = {.image_alloc = image_alloc,
                               .image_view_resource = image_view_resource};
        tex->staging_buffer = {
            .buffer = staging_buf, .allocation = staging_alloc, .size = total_size};
    };
    return err;
}

g_internal B32
texture_gpu_upload_cmd_recording(VkCommandBuffer cmd, render::Handle tex_handle, Buffer<U8> tex_buf)
{
    Context* vk_ctx = ctx_get();
    render::AssetItem<Texture>* tex_asset = (render::AssetItem<Texture>*)tex_handle.ptr;
    Texture* tex = &tex_asset->item;
    B32 err = false;

    bool is_ktx2 = ktx2_check(tex_buf.data, tex_buf.size);
    if (is_ktx2)
    {
        texture_ktx_cmd_record(cmd, tex, tex_buf);
    }
    else
    {
        err |= texture_cmd_record(cmd, tex, tex_buf);
        Assert(!err);
    }

    U32 cur_thread_id = os_tid();
    if (vk_ctx->render_thread_id == cur_thread_id)
    {
        tex->desc_set = descriptor_set_create(
            vk_ctx->arena, vk_ctx->device, vk_ctx->descriptor_pool, tex->desc_set_layout,
            tex->image_resource.image_view_resource.image_view, tex->sampler);
    }

    return err;
}

static ThreadInput*
thread_input_create()
{
    Arena* arena = ArenaAlloc();
    ThreadInput* thread_input = PushStruct(arena, ThreadInput);
    thread_input->arena = arena;
    return thread_input;
}

static void
thread_input_destroy(ThreadInput* thread_input)
{
    ArenaRelease(thread_input->arena);
}

static void
buffer_loading_thread(void* data, VkCommandBuffer cmd, render::Handle handle)
{
    render::BufferInfo* buffer_info = (render::BufferInfo*)data;
    Buffer<U8> buffer = buffer_info->buffer;
    Context* vk_ctx = ctx_get();

    AssetManager* asset_manager = asset_manager_get();
    render::AssetItem<BufferUpload>* asset_item_buffer =
        asset_manager_item_get(&asset_manager->buffer_list, handle);
    BufferUpload* asset_buffer = &asset_item_buffer->item;

    // ~mgj: copy to staging and record copy command
    BufferAllocation staging_buffer_alloc = staging_buffer_create(vk_ctx->allocator, buffer.size);

    VK_CHECK_RESULT(vmaCopyMemoryToAllocation(vk_ctx->allocator, buffer.data,
                                              staging_buffer_alloc.allocation, 0,
                                              staging_buffer_alloc.size));
    VkBufferCopy copy_region = {0};
    copy_region.size = staging_buffer_alloc.size;
    vkCmdCopyBuffer(cmd, staging_buffer_alloc.buffer, asset_buffer->buffer_alloc.buffer, 1,
                    &copy_region);

    asset_buffer->staging_buffer = staging_buffer_alloc;
}

static void
texture_loading_thread(void* data, VkCommandBuffer cmd, render::Handle handle)
{
    ScratchScope scratch = ScratchScope(0, 0);
    render::TextureLoadingInfo* extra_info = (render::TextureLoadingInfo*)data;
    Buffer<U8> tex_buf = io::file_read(scratch.arena, extra_info->tex_path);
    Assert(tex_buf.size > 0 && "Texture file not found");
    B32 err = texture_gpu_upload_cmd_recording(cmd, handle, tex_buf);
    if (err)
    {
        ERROR_LOG("Error when uploading texture - Error id: %d\n", err);
    }
}

g_internal void
texture_done_loading_thread(render::Handle handle)
{
    Context* vk_ctx = ctx_get();
    render::AssetItem<Texture>* asset = asset_manager_texture_item_get(handle);
    Texture* tex = &asset->item;
    tex->desc_set = descriptor_set_create(
        vk_ctx->arena, vk_ctx->device, vk_ctx->descriptor_pool, tex->desc_set_layout,
        tex->image_resource.image_view_resource.image_view, tex->sampler);
    buffer_destroy(vk_ctx->allocator, &asset->item.staging_buffer);
    asset->is_loaded = 1;
}

g_internal void
buffer_done_loading_thread(render::Handle handle)
{
    Context* vk_ctx = ctx_get();
    AssetManager* asset_manager = vk_ctx->asset_manager;

    render::AssetItem<BufferUpload>* asset =
        asset_manager_item_get(&asset_manager->buffer_list, handle);
    buffer_destroy(vk_ctx->allocator, &asset->item.staging_buffer);
    asset->item.staging_buffer.buffer = 0;
    asset->is_loaded = 1;
}

static void
colormap_loading_thread(void* data, VkCommandBuffer cmd, render::Handle handle)
{
    render::ColorMapLoadingInfo* colormap_info = (render::ColorMapLoadingInfo*)data;
    Buffer<U8> colormap_buf = {.data = (U8*)colormap_info->colormap_data,
                               .size = colormap_info->colormap_size};
    render::AssetItem<Texture>* asset = (render::AssetItem<Texture>*)handle.ptr;
    Assert(asset);
    colormap_texture_cmd_record(cmd, &asset->item, colormap_buf);
}

static void
colormap_done_loading_thread(render::Handle handle)
{
    Context* vk_ctx = ctx_get();
    render::AssetItem<Texture>* asset = asset_manager_texture_item_get(handle);
    Texture* tex = &asset->item;
    tex->desc_set = descriptor_set_create(
        vk_ctx->arena, vk_ctx->device, vk_ctx->descriptor_pool, tex->desc_set_layout,
        tex->image_resource.image_view_resource.image_view, tex->sampler);
    buffer_destroy(vk_ctx->allocator, &asset->item.staging_buffer);
    asset->is_loaded = 1;
}

static void
thread_main(async::ThreadInfo thread_info, void* input)
{
    ScratchScope scratch = ScratchScope(0, 0);
    ThreadInput* thread_input = (ThreadInput*)input;

    Context* vk_ctx = ctx_get();
    AssetManager* asset_store = vk_ctx->asset_manager;

    // ~mgj: Record the command buffer
    VkCommandBuffer cmd =
        begin_command(vk_ctx->device, asset_store->threaded_cmd_pools.data[thread_info.thread_id]);

    Assert(thread_input->handle.u64 != 0);
    Assert(thread_input->loading_func && thread_input->user_data);
    thread_input->loading_func(thread_input->user_data, cmd, thread_input->handle);

    VK_CHECK_RESULT(vkEndCommandBuffer(cmd));

    // ~mgj: Enqueue the command buffer
    asset_cmd_queue_item_enqueue(thread_info.thread_id, cmd, thread_input);
}

static Pipeline
model_3d_instance_pipeline_create(Context* vk_ctx, String8 shader_path)
{
    ScratchScope scratch = ScratchScope(0, 0);

    VkDescriptorSetLayoutBinding desc_set_layout_info = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
    };
    VkDescriptorSetLayout desc_set_layout =
        descriptor_set_layout_create(vk_ctx->device, &desc_set_layout_info, 1);

    String8 vert_path = CreatePathFromStrings(
        scratch.arena, Str8BufferFromCString(scratch.arena, {(char*)shader_path.str, "bin",
                                                             "model_3d_instancing_vert.spv"}));
    String8 frag_path = CreatePathFromStrings(
        scratch.arena, Str8BufferFromCString(scratch.arena, {(char*)shader_path.str, "bin",
                                                             "model_3d_instancing_frag.spv"}));

    ShaderModuleInfo vert_shader_stage_info = shader_stage_from_spirv(
        scratch.arena, vk_ctx->device, VK_SHADER_STAGE_VERTEX_BIT, vert_path);
    ShaderModuleInfo frag_shader_stage_info = shader_stage_from_spirv(
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

    U32 uv_offset = (U32)offsetof(render::Vertex3D, uv);
    U32 x_basis_offset = (U32)offsetof(render::Model3DInstance, x_basis);
    U32 y_basis_offset = (U32)offsetof(render::Model3DInstance, y_basis);
    U32 z_basis_offset = (U32)offsetof(render::Model3DInstance, z_basis);
    U32 w_basis_offset = (U32)offsetof(render::Model3DInstance, w_basis);

    VkVertexInputAttributeDescription attr_desc[] = {
        {.location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT},
        {.location = 1, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = uv_offset},
        {.location = 2,
         .binding = 1,
         .format = VK_FORMAT_R32G32B32A32_SFLOAT,
         .offset = x_basis_offset},
        {.location = 3,
         .binding = 1,
         .format = VK_FORMAT_R32G32B32A32_SFLOAT,
         .offset = y_basis_offset},
        {.location = 4,
         .binding = 1,
         .format = VK_FORMAT_R32G32B32A32_SFLOAT,
         .offset = z_basis_offset},
        {.location = 5,
         .binding = 1,
         .format = VK_FORMAT_R32G32B32A32_SFLOAT,
         .offset = w_basis_offset},
    };
    VkVertexInputBindingDescription input_desc[] = {{.binding = 0,
                                                     .stride = sizeof(render::Vertex3D),
                                                     .inputRate = VK_VERTEX_INPUT_RATE_VERTEX},
                                                    {.binding = 1,
                                                     .stride = sizeof(render::Model3DInstance),
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
        exit_with_error("failed to create pipeline layout!");
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
    pipeline_create_info.pDepthStencilState = &depth_stencil;
    pipeline_create_info.pColorBlendState = &color_blending;
    pipeline_create_info.pDynamicState = &dynamic_state;
    pipeline_create_info.layout = pipeline_layout;

    VkPipeline pipeline;
    if (vkCreateGraphicsPipelines(vk_ctx->device, VK_NULL_HANDLE, 1, &pipeline_create_info, nullptr,
                                  &pipeline) != VK_SUCCESS)
    {
        exit_with_error("failed to create graphics pipeline!");
    }

    Buffer<VkDescriptorSetLayout> desc_set_layouts =
        BufferAlloc<VkDescriptorSetLayout>(vk_ctx->arena, 1);
    desc_set_layouts.data[0] = desc_set_layout;

    Pipeline pipeline_info = {.pipeline = pipeline,
                              .pipeline_layout = pipeline_layout,
                              .descriptor_set_layout = desc_set_layouts};
    return pipeline_info;
}

static Pipeline
model_3d_pipeline_create(Context* vk_ctx, String8 shader_path)
{
    ScratchScope scratch = ScratchScope(0, 0);

    VkDescriptorSetLayoutBinding desc_set_layout_info = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
    };

    VkDescriptorSetLayout desc_set_layout =
        descriptor_set_layout_create(vk_ctx->device, &desc_set_layout_info, 1);

    String8 vert_path = CreatePathFromStrings(
        scratch.arena,
        Str8BufferFromCString(scratch.arena, {(char*)shader_path.str, "bin", "model_3d_vert.spv"}));
    String8 frag_path = CreatePathFromStrings(
        scratch.arena,
        Str8BufferFromCString(scratch.arena, {(char*)shader_path.str, "bin", "model_3d_frag.spv"}));

    ShaderModuleInfo vert_shader_stage_info = shader_stage_from_spirv(
        scratch.arena, vk_ctx->device, VK_SHADER_STAGE_VERTEX_BIT, vert_path);
    ShaderModuleInfo frag_shader_stage_info = shader_stage_from_spirv(
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

    U32 uv_offset = offsetof(render::Vertex3D, uv);
    U32 object_id_offset = offsetof(render::Vertex3D, object_id);

    VkVertexInputAttributeDescription attr_desc[] = {
        {.location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT},
        {.location = 1, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = uv_offset},
        {.location = 2,
         .binding = 0,
         .format = vk_ctx->object_id_format,
         .offset = object_id_offset}};

    VkVertexInputBindingDescription input_desc[] = {{.binding = 0,
                                                     .stride = sizeof(render::Vertex3D),
                                                     .inputRate = VK_VERTEX_INPUT_RATE_VERTEX}};

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
        exit_with_error("failed to create pipeline layout!");
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
    pipeline_create_info.pDepthStencilState = &depth_stencil;
    pipeline_create_info.pColorBlendState = &color_blending;
    pipeline_create_info.pDynamicState = &dynamic_state;
    pipeline_create_info.layout = pipeline_layout;

    VkPipeline pipeline;
    if (vkCreateGraphicsPipelines(vk_ctx->device, VK_NULL_HANDLE, 1, &pipeline_create_info, nullptr,
                                  &pipeline) != VK_SUCCESS)
    {
        exit_with_error("failed to create graphics pipeline!");
    }

    Buffer<VkDescriptorSetLayout> desc_layouts =
        BufferAlloc<VkDescriptorSetLayout>(vk_ctx->arena, 1);
    desc_layouts.data[0] = desc_set_layout;

    Pipeline pipeline_info = {.pipeline = pipeline,
                              .pipeline_layout = pipeline_layout,
                              .descriptor_set_layout = desc_layouts};
    return pipeline_info;
}

static Pipeline
blend_3d_pipeline_create(String8 shader_path)
{
    Context* vk_ctx = ctx_get();
    ScratchScope scratch = ScratchScope(0, 0);

    VkDescriptorSetLayoutBinding tex_desc_set_layout_info[] = {{
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
    }};
    VkDescriptorSetLayout tex_desc_set_layout = descriptor_set_layout_create(
        vk_ctx->device, tex_desc_set_layout_info, ArrayCount(tex_desc_set_layout_info));

    VkDescriptorSetLayoutBinding colormap_desc_set_layout_info[] = {{
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
    }};
    VkDescriptorSetLayout colormap_desc_set_layout = descriptor_set_layout_create(
        vk_ctx->device, colormap_desc_set_layout_info, ArrayCount(colormap_desc_set_layout_info));

    String8 vert_path = CreatePathFromStrings(
        scratch.arena, Str8BufferFromCString(scratch.arena, {(char*)shader_path.str, "bin",
                                                             "colormap_blend_3d_vert.spv"}));
    String8 frag_path = CreatePathFromStrings(
        scratch.arena, Str8BufferFromCString(scratch.arena, {(char*)shader_path.str, "bin",
                                                             "colormap_blend_3d_frag.spv"}));

    ShaderModuleInfo vert_shader_stage_info = shader_stage_from_spirv(
        scratch.arena, vk_ctx->device, VK_SHADER_STAGE_VERTEX_BIT, vert_path);
    ShaderModuleInfo frag_shader_stage_info = shader_stage_from_spirv(
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

    U32 uv_offset = offsetof(render::Vertex3DBlend, uv);
    U32 object_id_offset = offsetof(render::Vertex3DBlend, object_id);
    U32 color_offset = offsetof(render::Vertex3DBlend, blend_factor);

    VkVertexInputAttributeDescription attr_desc[] = {
        {.location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT},
        {.location = 1, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = uv_offset},
        {.location = 2,
         .binding = 0,
         .format = vk_ctx->object_id_format,
         .offset = object_id_offset},
        {.location = 3, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = color_offset}};

    VkVertexInputBindingDescription input_desc[] = {{.binding = 0,
                                                     .stride = sizeof(render::Vertex3DBlend),
                                                     .inputRate = VK_VERTEX_INPUT_RATE_VERTEX}};

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

    VkDescriptorSetLayout descriptor_set_layouts[] = {
        vk_ctx->camera_descriptor_set_layout, tex_desc_set_layout, colormap_desc_set_layout};
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
    VK_CHECK_RESULT(
        vkCreatePipelineLayout(vk_ctx->device, &pipelineLayoutInfo, nullptr, &pipeline_layout));

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
    pipeline_create_info.pDepthStencilState = &depth_stencil;
    pipeline_create_info.pColorBlendState = &color_blending;
    pipeline_create_info.pDynamicState = &dynamic_state;
    pipeline_create_info.layout = pipeline_layout;

    VkPipeline pipeline;
    VK_CHECK_RESULT(vkCreateGraphicsPipelines(vk_ctx->device, VK_NULL_HANDLE, 1,
                                              &pipeline_create_info, nullptr, &pipeline));

    Buffer<VkDescriptorSetLayout> desc_layouts =
        BufferAlloc<VkDescriptorSetLayout>(vk_ctx->arena, 2);
    desc_layouts.data[0] = tex_desc_set_layout;
    desc_layouts.data[1] = colormap_desc_set_layout;

    Pipeline pipeline_info = {.pipeline = pipeline,
                              .pipeline_layout = pipeline_layout,
                              .descriptor_set_layout = desc_layouts};
    return pipeline_info;
}

static void
model_3d_instance_rendering()
{
    Context* vk_ctx = ctx_get();
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
    buffer_alloc_create_or_resize(vk_ctx->allocator,
                                  model_3D_instance_draw->total_instance_buffer_byte_count,
                                  instance_buffer_alloc, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

    for (Model3DInstanceNode* node = vk_ctx->draw_frame->model_3D_instance_draw.list.first; node;
         node = node->next)
    {
        VK_CHECK_RESULT(vmaCopyMemoryToAllocation(
            vk_ctx->allocator, node->instance_buffer_info.buffer.data,
            instance_buffer_alloc->allocation, node->instance_buffer_offset,
            node->instance_buffer_info.buffer.size));
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
camera_uniform_buffer_create(Context* vk_ctx)
{
    VkDeviceSize camera_buffer_size = sizeof(CameraUniformBuffer);
    VkBufferUsageFlags buffer_usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

    for (U32 i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        vk_ctx->camera_buffer_alloc_mapped[i] =
            buffer_mapped_create(vk_ctx->allocator, camera_buffer_size, buffer_usage);
    }
}

static void
camera_uniform_buffer_update(Context* vk_ctx, ui::Camera* camera, Vec2F32 screen_res,
                             U32 current_frame)
{
    BufferAllocationMapped* buffer = &vk_ctx->camera_buffer_alloc_mapped[current_frame];
    CameraUniformBuffer* ubo = (CameraUniformBuffer*)buffer->mapped_ptr;
    glm::mat4 transform = camera->projection_matrix * camera->view_matrix;
    frustum_planes_calculate(&ubo->frustum, transform);
    ubo->viewport_dim.x = screen_res.x;
    ubo->viewport_dim.y = screen_res.y;
    ubo->view = camera->view_matrix;
    ubo->proj = camera->projection_matrix;

    buffer_mapped_update(vk_ctx->command_buffers.data[current_frame], vk_ctx->allocator, *buffer);
}

static void
camera_descriptor_set_layout_create(Context* vk_ctx)
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
        exit_with_error("failed to create camera descriptor set layout!");
    }
}

static void
camera_descriptor_set_create(Context* vk_ctx)
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
        exit_with_error("CameraDescriptorSetCreate: failed to allocate descriptor sets!");
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
camera_cleanup(Context* vk_ctx)
{
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        buffer_mapped_destroy(vk_ctx->allocator, &vk_ctx->camera_buffer_alloc_mapped[i]);
    }

    vkDestroyDescriptorSetLayout(vk_ctx->device, vk_ctx->camera_descriptor_set_layout, NULL);
}

// ~mgj: Asset Streaming
g_internal AssetManager*
asset_manager_get()
{
    Context* vk_ctx = ctx_get();
    return vk_ctx->asset_manager;
}

// ~mgj: Deferred Deletion Queue Implementation
static void
deletion_queue_push(DeletionQueue* queue, render::Handle handle, render::AssetItemType type,
                    U64 frames_in_flight)
{
    AssetManager* asset_manager = asset_manager_get();
    PendingDeletion* deletion = nullptr;

    if (queue->free_list)
    {
        deletion = queue->free_list;
        SLLStackPop(queue->free_list);
        MemoryZero(deletion, sizeof(*deletion));
    }
    else
    {
        deletion = PushStruct(asset_manager->arena, PendingDeletion);
    }

    deletion->handle = handle;
    deletion->type = type;
    deletion->frame_to_delete = queue->frame_counter + frames_in_flight;

    SLLQueuePush(queue->first, queue->last, deletion);

    DEBUG_LOG("Asset ID: %llu - Queued for deferred deletion at frame %llu (current: %llu)",
              handle.u64, deletion->frame_to_delete, queue->frame_counter);
}

static void
deletion_queue_resource_free(PendingDeletion* deletion)
{
    Context* vk_ctx = ctx_get();
    switch (deletion->type)
    {
        case render::AssetItemType_Buffer:
        {
            asset_manager_buffer_free(deletion->handle);
        }
        break;
        case render::AssetItemType_Texture:
        {
            asset_manager_texture_free(deletion->handle);
        }
        break;
        default: InvalidPath; break;
    }
}

static void
deletion_queue_deferred_resource_deletion(DeletionQueue* queue)
{
    Context* vk_ctx = ctx_get();

    for (PendingDeletion* deletion = queue->first;
         deletion && deletion->frame_to_delete <= queue->frame_counter; deletion = queue->first)
    {
        SLLQueuePop(queue->first, queue->last);
        DEBUG_LOG("Asset ID: %llu - Executing deferred deletion at frame %llu",
                  deletion->handle.u64, queue->frame_counter);

        deletion_queue_resource_free(deletion);
        SLLStackPush(queue->free_list, deletion);
    }

    queue->frame_counter++;
}

static void
deletion_queue_delete_all(DeletionQueue* queue)
{
    Context* vk_ctx = ctx_get();

    for (PendingDeletion* deletion = queue->first; deletion; deletion = queue->first)
    {
        SLLQueuePop(queue->first, queue->last);
        DEBUG_LOG("Asset ID: %llu - Force deleting from deletion queue", deletion->handle.u64);

        deletion_queue_resource_free(deletion);

        SLLStackPush(queue->free_list, deletion);
    }
}

static AssetManager*
asset_manager_create(VkDevice device, U32 queue_family_index, async::Threads* threads,
                     U64 total_size_in_bytes)
{
    Context* vk_ctx = ctx_get();
    Arena* arena = ArenaAlloc();
    AssetManager* asset_store = PushStruct(arena, AssetManager);
    asset_store->arena = arena;
    asset_store->total_size = total_size_in_bytes;
    asset_store->work_queue = threads->msg_queue;
    asset_store->threaded_cmd_pools =
        BufferAlloc<AssetManagerCommandPool>(arena, threads->thread_handles.size);
    asset_store->texture_free_list = nullptr;

    VkCommandPoolCreateInfo cmd_pool_info{};
    cmd_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cmd_pool_info.queueFamilyIndex = queue_family_index;
    cmd_pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;

    for (U32 i = 0; i < threads->thread_handles.size; i++)
    {
        asset_store->threaded_cmd_pools.data[i].cmd_pool =
            command_pool_create(device, &cmd_pool_info);
        asset_store->threaded_cmd_pools.data[i].mutex = OS_MutexAlloc();
    }

    asset_store->cmd_wait_list = asset_manager_cmd_list_create();
    U32 cmd_queue_size = 10;
    asset_store->cmd_queue = async::QueueInit<CmdQueueItem>(
        arena, cmd_queue_size, vk_ctx->queue_family_indices.graphicsFamilyIndex);

    asset_store->deletion_queue = PushStruct(arena, DeletionQueue);

    return asset_store;
}

static void
asset_manager_destroy(Context* vk_ctx, AssetManager* asset_store)
{
    for (U32 i = 0; i < asset_store->threaded_cmd_pools.size; i++)
    {
        vkDestroyCommandPool(vk_ctx->device, asset_store->threaded_cmd_pools.data[i].cmd_pool, 0);
        OS_MutexRelease(asset_store->threaded_cmd_pools.data[i].mutex);
    }
    async::QueueDestroy(asset_store->cmd_queue);
    asset_manager_cmd_list_destroy(asset_store->cmd_wait_list);
    deletion_queue_delete_all(asset_store->deletion_queue);

    ArenaRelease(asset_store->arena);
}

static void
asset_cmd_queue_item_enqueue(U32 thread_id, VkCommandBuffer cmd, ThreadInput* thread_input)
{
    AssetManager* asset_manager = ctx_get()->asset_manager;

    CmdQueueItem item = {.thread_input = thread_input, .thread_id = thread_id, .cmd_buffer = cmd};
    DEBUG_LOG("Asset ID: %llu - Cmd Getting Queued", thread_input->handle.u64);
    async::QueuePush(asset_manager->cmd_queue, &item);
}

static void
asset_manager_execute_cmds()
{
    Context* vk_ctx = ctx_get();
    AssetManager* asset_store = vk_ctx->asset_manager;

    for (U32 i = 0; i < asset_store->cmd_queue->queue_size; i++)
    {
        CmdQueueItem item;
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

            DEBUG_LOG("Asset ID: %llu - Submitted Command Buffer", item.thread_input->handle.u64);
            asset_manager_cmd_list_add(asset_store->cmd_wait_list, item);
        }
    }
}

template <typename T>
static render::AssetItem<T>*
asset_manager_item_get(render::AssetItemList<T>* list, render::Handle handle)
{
    Assert(handle.u64 != 0);
    for (render::AssetItem<T>* item = list->first; item; item = item->next)
    {
        if (handle.u64 == (U64)item)
        {
            return item;
        }
    }

    if (!render::is_handle_zero(handle))
    {
        exit_with_error("Asset ID: %llu - Not Found", handle.u64);
    }

    return 0;
}

static render::AssetItem<Texture>*
asset_manager_texture_item_get(render::Handle handle)
{
    Context* vk_ctx = ctx_get();
    AssetManager* asset_manager = vk_ctx->asset_manager;

    render::AssetItem<Texture>* asset_item_texture =
        asset_manager_item_get(&asset_manager->texture_list, handle);
    return asset_item_texture;
}

template <typename T>
static render::AssetItem<T>*
asset_manager_item_create(render::AssetItemList<T>* list, render::AssetItem<T>** free_list)
{
    Context* vk_ctx = ctx_get();
    AssetManager* asset_mng = vk_ctx->asset_manager;
    Arena* arena = asset_mng->arena;

    render::AssetItem<T>* asset_item = *free_list;
    if (asset_item)
    {
        SLLStackPop(*free_list);
        MemoryZero(asset_item, sizeof(*asset_item));
    }
    else
    {
        asset_item = PushStruct(arena, render::AssetItem<T>);
    }
    Assert(asset_item);

    DLLPushFront(list->first, list->last, asset_item);
    return asset_item;
}

static void
asset_manager_cmd_done_check()
{
    Context* vk_ctx = ctx_get();
    AssetManager* asset_manager = vk_ctx->asset_manager;
    for (CmdQueueItem* cmd_queue_item = asset_manager->cmd_wait_list->list_first; cmd_queue_item;)
    {
        CmdQueueItem* next = cmd_queue_item->next;
        VkResult result = vkGetFenceStatus(vk_ctx->device, cmd_queue_item->fence);
        if (result == VK_SUCCESS)
        {
            OS_MutexScope(asset_manager->threaded_cmd_pools.data[cmd_queue_item->thread_id].mutex)
            {
                vkFreeCommandBuffers(
                    vk_ctx->device,
                    asset_manager->threaded_cmd_pools.data[cmd_queue_item->thread_id].cmd_pool, 1,
                    &cmd_queue_item->cmd_buffer);
            }

            ThreadInput* thread_input = cmd_queue_item->thread_input;

            Assert(thread_input->done_loading_func != nullptr);
            thread_input->done_loading_func(thread_input->handle);

            DEBUG_LOG("Asset: %llu - Finished loading", thread_input->handle.u64);
            vkDestroyFence(vk_ctx->device, cmd_queue_item->fence, 0);
            thread_input_destroy(cmd_queue_item->thread_input);
            asset_manager_cmd_list_item_remove(asset_manager->cmd_wait_list, cmd_queue_item);
        }
        else if (result != VK_NOT_READY)
        {
            VK_CHECK_RESULT(result);
        }
        cmd_queue_item = next;
    }
}

static VkCommandBuffer
begin_command(VkDevice device, AssetManagerCommandPool threaded_cmd_pool)
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
asset_manager_cmd_list_create()
{
    Arena* arena = ArenaAlloc();
    AssetManagerCmdList* cmd_list = PushStruct(arena, AssetManagerCmdList);
    cmd_list->arena = arena;
    return cmd_list;
}

static void
asset_manager_cmd_list_destroy(AssetManagerCmdList* cmd_wait_list)
{
    while (cmd_wait_list->list_first)
    {
        asset_manager_cmd_done_check();
    }
    ArenaRelease(cmd_wait_list->arena);
}

static void
asset_manager_cmd_list_add(AssetManagerCmdList* cmd_list, CmdQueueItem item)
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
asset_manager_cmd_list_item_remove(AssetManagerCmdList* cmd_list, CmdQueueItem* item)
{
    DLLRemove(cmd_list->list_first, cmd_list->list_last, item);
    MemoryZeroStruct(item);
    SLLStackPush(cmd_list->free_list, item);
}

static void
asset_manager_buffer_free(render::Handle handle)
{
    Context* vk_ctx = ctx_get();
    AssetManager* asset_manager = vk_ctx->asset_manager;
    render::AssetItem<BufferUpload>* item =
        asset_manager_item_get(&asset_manager->buffer_list, handle);
    if (item->is_loaded)
    {
        buffer_destroy(vk_ctx->allocator, &item->item.buffer_alloc);
    }
    item->is_loaded = false;
    DLLRemove(asset_manager->buffer_list.first, asset_manager->buffer_list.last, item);
    SLLStackPush(vk_ctx->asset_manager->buffer_free_list, item);
}

static void
asset_manager_texture_free(render::Handle handle)
{
    Context* vk_ctx = ctx_get();
    AssetManager* asset_manager = vk_ctx->asset_manager;
    render::AssetItem<Texture>* item = asset_manager_item_get(&asset_manager->texture_list, handle);
    if (item->is_loaded)
    {
        texture_destroy(&item->item);
    }
    item->is_loaded = false;
    DLLRemove(asset_manager->texture_list.first, asset_manager->texture_list.last, item);
    SLLStackPush(vk_ctx->asset_manager->texture_free_list, item);
}

// ~mgj: Rendering
static void
draw_frame_reset()
{
    Context* vk_ctx = ctx_get();
    ArenaClear(vk_ctx->draw_frame_arena);
    vk_ctx->draw_frame = PushStruct(vk_ctx->draw_frame_arena, DrawFrame);
}

static void
pipeline_destroy(Pipeline* pipeline)
{
    Context* vk_ctx = ctx_get();
    vkDestroyPipeline(vk_ctx->device, pipeline->pipeline, NULL);
    vkDestroyPipelineLayout(vk_ctx->device, pipeline->pipeline_layout, NULL);
    for (U32 i = 0; i < pipeline->descriptor_set_layout.size; i++)
    {
        vkDestroyDescriptorSetLayout(vk_ctx->device, pipeline->descriptor_set_layout.data[i], NULL);
    }
}

static void
model_3d_bucket_add(BufferAllocation* vertex_buffer_allocation,
                    BufferAllocation* index_buffer_allocation, VkDescriptorSet texture_handle,
                    B32 depth_write_per_draw_call_only, U32 index_buffer_offset, U32 index_count)
{
    Context* vk_ctx = ctx_get();
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
model_3d_instance_bucket_add(BufferAllocation* vertex_buffer_allocation,
                             BufferAllocation* index_buffer_allocation,
                             VkDescriptorSet texture_handle,
                             render::BufferInfo* instance_buffer_info)
{
    U32 align = 16;
    Context* vk_ctx = ctx_get();
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
blend_3d_bucket_add(BufferAllocation* vertex_buffer_allocation,
                    BufferAllocation* index_buffer_allocation, VkDescriptorSet texture_handle,
                    VkDescriptorSet colormap_handle)
{
    Context* vk_ctx = ctx_get();
    DrawFrame* draw_frame = vk_ctx->draw_frame;

    Blend3DNode* node = PushStruct(vk_ctx->draw_frame_arena, Blend3DNode);
    node->vertex_alloc = *vertex_buffer_allocation;
    node->index_alloc = *index_buffer_allocation;
    node->texture_handle = texture_handle;
    node->colormap_handle = colormap_handle;

    SLLQueuePush(draw_frame->blend_3d_list.first, draw_frame->blend_3d_list.last, node);
}

enum WriteType
{
    WriteType_Color,
    WriteType_Depth,
    WriteType_Count
};

g_internal void
draw_indexed_separate_depth_and_color_calls(VkCommandBuffer cmd_buffer, U32 index_offset,
                                            U32 index_count)
{
    VkBool32 color_write_enabled[4] = {VK_TRUE, VK_TRUE, VK_TRUE, VK_TRUE};
    VkBool32 color_write_disabled[4] = {};
    for (WriteType write_type = (WriteType)0; write_type < WriteType_Count;
         write_type = (WriteType)(write_type + 1))
    {
        if (write_type == WriteType_Color)
        {
            vkCmdSetDepthWriteEnable(cmd_buffer, VK_FALSE);
            cmd_set_color_write_enable_ext(cmd_buffer, ArrayCount(color_write_enabled),
                                           color_write_enabled);
        }
        else if (write_type == WriteType_Depth)
        {
            vkCmdSetDepthWriteEnable(cmd_buffer, VK_TRUE);
            cmd_set_color_write_enable_ext(cmd_buffer, ArrayCount(color_write_disabled),
                                           color_write_disabled);
        }

        vkCmdDrawIndexed(cmd_buffer, index_count, 1, index_offset, 0, 0);
    }
}

static void
model_3d_rendering()
{
    Context* vk_ctx = ctx_get();
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

    VkDescriptorSet descriptor_sets[2] = {vk_ctx->camera_descriptor_sets[vk_ctx->current_frame]};

    VkDeviceSize offsets[] = {0};
    for (Model3DNode* node = draw_frame->model_3D_list.first; node; node = node->next)
    {
        descriptor_sets[1] = node->texture_handle;
        vkCmdBindDescriptorSets(cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                model_3D_pipeline->pipeline_layout, 0, ArrayCount(descriptor_sets),
                                descriptor_sets, 0, NULL);
        vkCmdBindVertexBuffers(cmd_buffer, 0, 1, &node->vertex_alloc.buffer, offsets);
        vkCmdBindIndexBuffer(cmd_buffer, node->index_alloc.buffer, 0, VK_INDEX_TYPE_UINT32);
        VkBool32 color_write_enabled[4] = {VK_TRUE, VK_TRUE, VK_TRUE, VK_TRUE};
        VkBool32 color_write_disabled[4] = {};
        if (node->depth_write_per_draw_enabled)
        {
            draw_indexed_separate_depth_and_color_calls(cmd_buffer, node->index_buffer_offset,
                                                        node->index_count);
        }
        else
        {
            vkCmdSetDepthWriteEnable(cmd_buffer, VK_TRUE);
            cmd_set_color_write_enable_ext(cmd_buffer, ArrayCount(color_write_enabled),
                                           color_write_enabled);
            vkCmdDrawIndexed(cmd_buffer, node->index_count, 1, node->index_buffer_offset, 0, 0);
        }
    }
}

static void
blend_3d_rendering()
{
    Context* vk_ctx = ctx_get();
    Pipeline* blend_3d_pipeline = &vk_ctx->blend_3d_pipeline;
    DrawFrame* draw_frame = vk_ctx->draw_frame;

    SwapchainResources* swapchain_resources = vk_ctx->swapchain_resources;
    VkExtent2D swapchain_extent = swapchain_resources->swapchain_extent;
    VkCommandBuffer cmd_buffer = vk_ctx->command_buffers.data[vk_ctx->current_frame];
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

    VkDescriptorSet descriptor_sets[3] = {vk_ctx->camera_descriptor_sets[vk_ctx->current_frame]};

    VkDeviceSize offsets[] = {0};
    for (Blend3DNode* node = draw_frame->blend_3d_list.first; node; node = node->next)
    {
        descriptor_sets[1] = node->texture_handle;
        descriptor_sets[2] = node->colormap_handle;

        U32 index_count = node->index_alloc.size / sizeof(U32);
        vkCmdBindDescriptorSets(cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                blend_3d_pipeline->pipeline_layout, 0, ArrayCount(descriptor_sets),
                                descriptor_sets, 0, NULL);
        vkCmdBindVertexBuffers(cmd_buffer, 0, 1, &node->vertex_alloc.buffer, offsets);
        vkCmdBindIndexBuffer(cmd_buffer, node->index_alloc.buffer, 0, VK_INDEX_TYPE_UINT32);

        draw_indexed_separate_depth_and_color_calls(cmd_buffer, 0, index_count);
    }
}

static void
command_buffer_record(U32 image_index, U32 current_frame, ui::Camera* camera,
                      Vec2S64 mouse_cursor_pos)
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
    ImageResource* object_id_image_resource =
        &swapchain_resource->object_id_image_resources.data[image_index];
    ImageAllocation* object_id_image_alloc = &object_id_image_resource->image_alloc;
    VkImageView object_id_image_view = object_id_image_resource->image_view_resource.image_view;
    VkImage object_id_image = object_id_image_alloc->image;

    // object id resolve image
    ImageResource* object_id_image_resolve_resource =
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
        {
            TracyVkZone(vk_ctx->tracy_ctx[current_frame], current_cmd_buf, "Render"); // NOLINT

            VkExtent2D swapchain_extent = swapchain_resource->swapchain_extent;
            VkImageView color_image_view =
                swapchain_resource->color_image_resource.image_view_resource.image_view;
            VkImageView depth_image_view =
                swapchain_resource->depth_image_resource.image_view_resource.image_view;

            VkClearColorValue clear_color = {0.0f, 0.0f, 0.0f, 1.0f};
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

            camera_uniform_buffer_update(
                vk_ctx, camera,
                Vec2F32{(F32)vk_ctx->swapchain_resources->swapchain_extent.width,
                        (F32)vk_ctx->swapchain_resources->swapchain_extent.height},
                current_frame);

            vkCmdBeginRendering(current_cmd_buf, &rendering_info);

            model_3d_instance_rendering();
            model_3d_rendering();
            blend_3d_rendering();

            vkCmdEndRendering(current_cmd_buf);

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
            exit_with_error("failed to record command buffer!");
        }
    }
    ScratchEnd(scratch);
}

static void
profile_buffers_create(Context* vk_ctx)
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

namespace vulkan
{

// ~mgj: Global asset manager pointer
static AssetManager* g_asset_manager = nullptr;

g_internal AssetManager*
asset_manager_get()
{
    Assert(g_asset_manager);
    return g_asset_manager;
}

static void
texture_destroy(Texture* texture)
{
    AssetManager* asset_manager = asset_manager_get();
    buffer_destroy(&texture->staging_allocation);
    image_resource_destroy(texture->image_resource);
    vkDestroySampler(asset_manager->device, texture->sampler, nullptr);
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
texture_ktx_cmd_record(VkCommandBuffer cmd, Texture* tex, Buffer<U8> tex_buf)
{
    ScratchScope scratch = ScratchScope(0, 0);
    AssetManager* asset_manager = asset_manager_get();

    ktxTexture2* ktx_texture;
    ktx_error_code_e ktxresult =
        ktxTexture2_CreateFromMemory(tex_buf.data, tex_buf.size, NULL, &ktx_texture);

    if (ktxresult == KTX_SUCCESS)
    {
        U32 mip_levels = ktx_texture->numLevels;
        U32 base_width = ktx_texture->baseWidth;
        U32 base_height = ktx_texture->baseHeight;
        U32 base_depth = ktx_texture->baseDepth;

        Buffer<U32> mip_level_offsets = BufferAlloc<U32>(scratch.arena, mip_levels);
        for (U32 i = 0; i < ktx_texture->numLevels; ++i)
        {
            ktx_size_t offset;
            KTX_error_code ktx_error =
                ktxTexture_GetImageOffset((ktxTexture*)ktx_texture, i, 0, 0, &offset);
            if (ktx_error != KTX_SUCCESS)
            {
                exit_with_error("Failed to get image offset for mipmap level %u", i);
            }
            mip_level_offsets.data[i] = (U32)offset;
        }

        VkFormat vk_format = ktxTexture2_GetVkFormat(ktx_texture);

        BufferAllocation staging_allocation = staging_buffer_mapped_create(ktx_texture->dataSize);
        VmaAllocationInfo staging_allocation_info;
        vmaGetAllocationInfo(asset_manager->allocator, staging_allocation.allocation,
                             &staging_allocation_info);

        VmaAllocationCreateInfo vma_info = {.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
        ImageAllocation image_alloc = image_allocation_create(
            base_width, base_height, VK_SAMPLE_COUNT_1_BIT, vk_format, VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                VK_IMAGE_USAGE_SAMPLED_BIT,
            mip_levels, vma_info);

        ImageViewResource image_view_resource = image_view_resource_create(
            asset_manager->device, image_alloc.image, VK_FORMAT_R8G8B8A8_SRGB,
            VK_IMAGE_ASPECT_COLOR_BIT, mip_levels);

        KTX_error_code ktx_error = ktxTexture_LoadImageData(
            (ktxTexture*)ktx_texture, (U8*)staging_allocation_info.pMappedData,
            ktx_texture->dataSize);
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
        vkCmdCopyBufferToImage(cmd, staging_allocation.buffer, image_alloc.image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mip_levels, regions);

        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, 1,
                             &barrier);

        tex->image_resource = ImageResource(image_alloc, image_view_resource);
        tex->staging_allocation = staging_allocation;

        ktxTexture_Destroy((ktxTexture*)ktx_texture);
    }
    Assert(ktxresult == KTX_SUCCESS);
}

g_internal void
colormap_texture_cmd_record(VkCommandBuffer cmd, Texture* tex, Buffer<U8> buf)
{
    ScratchScope scratch = ScratchScope(0, 0);
    AssetManager* asset_manager = asset_manager_get();
    U32 sizeof_rgb = 3;
    U32 sizeof_f32 = sizeof(F32);
    U32 colormap_size = buf.size / sizeof_rgb / sizeof_f32;
    U32 staging_buffer_size = colormap_size * 4 * sizeof(F32);
    AssertAlways(colormap_size == 256);
    VkFormat vk_format = VK_FORMAT_R32G32B32A32_SFLOAT;

    BufferAllocation staging_allocation = staging_buffer_mapped_create(staging_buffer_size);
    VmaAllocationInfo staging_allocation_info;
    vmaGetAllocationInfo(asset_manager->allocator, staging_allocation.allocation,
                         &staging_allocation_info);

    // Convert RGB to RGBA by adding alpha channel (1.0)
    F32* src = (F32*)buf.data;
    F32* dst = (F32*)staging_allocation_info.pMappedData;
    for (U32 i = 0; i < colormap_size; i++)
    {
        dst[i * 4 + 0] = src[i * 3 + 0]; // R
        dst[i * 4 + 1] = src[i * 3 + 1]; // G
        dst[i * 4 + 2] = src[i * 3 + 2]; // B
        dst[i * 4 + 3] = 1.0f;           // A
    }

    U32 mip_levels = 1;
    VmaAllocationCreateInfo vma_info = {.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
    ImageAllocation image_alloc = image_allocation_create(
        colormap_size, 1, VK_SAMPLE_COUNT_1_BIT, vk_format, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
            VK_IMAGE_USAGE_SAMPLED_BIT,
        mip_levels, vma_info, VK_IMAGE_TYPE_1D);

    ImageViewResource image_view_resource =
        image_view_resource_create(asset_manager->device, image_alloc.image, vk_format,
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
    vkCmdCopyBufferToImage(cmd, staging_allocation.buffer, image_alloc.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mip_levels, region);

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, NULL, 0, NULL, 1, &barrier);

    tex->image_resource = ImageResource(image_alloc, image_view_resource);
    tex->staging_allocation = staging_allocation;
}

g_internal ImageAllocationResource
texture_upload_with_blitting(VkCommandBuffer cmd, render::TextureUploadData* data)
{
    AssetManager* asset_manager = asset_manager_get();
    U32 mip_levels = floor(log2f(Max(data->width, data->height))) + 1;
    VkFormat vk_format = VK_FORMAT_R8G8B8A8_SRGB;

    BufferAllocation staging_allocation = staging_buffer_mapped_create(data->data_byte_size);
    // upload base mip level

    VmaAllocationCreateInfo vma_info = {.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
    ImageAllocation image_alloc = image_allocation_create(
        data->width, data->height, VK_SAMPLE_COUNT_1_BIT, vk_format, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
            VK_IMAGE_USAGE_SAMPLED_BIT,
        mip_levels, vma_info);

    // TODO: Implement error handling
    VK_CHECK_RESULT(vmaCopyMemoryToAllocation(asset_manager->allocator, data->data,
                                              staging_allocation.allocation, 0,
                                              data->data_byte_size));

    ImageViewResource image_view_resource = image_view_resource_create(
        asset_manager->device, image_alloc.image, vk_format, VK_IMAGE_ASPECT_COLOR_BIT, mip_levels);

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

    VkBufferImageCopy region;
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                               .mipLevel = 0,
                               .baseArrayLayer = 0,
                               .layerCount = 1};
    region.imageOffset = {.x = 0, .y = 0, .z = 0};
    region.imageExtent = {.width = (U32)data->width, .height = (U32)data->height, .depth = 1};

    vkCmdCopyBufferToImage(cmd, staging_allocation.buffer, image_alloc.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    S32 dst_img_width = data->width;
    S32 dst_img_height = data->height;
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

    ImageAllocationResource image_allocation_resource = ImageAllocationResource(
        ImageResource(image_alloc, image_view_resource), staging_allocation);
    return image_allocation_resource;
}

g_internal B32
texture_cmd_record_with_stb(VkCommandBuffer cmd, Texture* tex, Buffer<U8> tex_buf)
{
    S32 width;
    S32 height;
    S32 desired_num_channels = STBI_rgb_alpha;
    U8* image_data = stbi_load_from_memory(tex_buf.data, tex_buf.size, &width, &height, NULL,
                                           desired_num_channels);
    B32 err = false;
    err = !image_data;

    Assert(image_data);
    if (image_data)
    {
        constexpr U32 bytes_per_pixel = 1;
        render::TextureUploadData tex_data = render::TextureUploadData::init(
            image_data, (U32)width, (U32)height, (U32)desired_num_channels, bytes_per_pixel);
        ImageAllocationResource image_allocation_resource =
            texture_upload_with_blitting(cmd, &tex_data);
        tex->image_resource = image_allocation_resource.image_resource;
        tex->staging_allocation = image_allocation_resource.staging_buffer_alloc;
    };
    return err;
}

g_internal B32
texture_gpu_upload_cmd_recording(VkCommandBuffer cmd, render::Handle tex_handle, Buffer<U8> tex_buf)
{
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
        err |= texture_cmd_record_with_stb(cmd, tex, tex_buf);
        Assert(!err);
    }

    return err;
}

g_internal U32
buffer_allocation_size_get(BufferAllocation* buffer_allocation)
{
    if (!buffer_allocation->allocation)
    {
        return 0;
    }

    return buffer_allocation->allocation->GetSize();
}

static void
buffer_loading_thread(void* data, render::ThreadInput* thread_input)
{
    Assert(thread_input->handles.count == 1);
    render::Handle handle = render::handle_list_first_handle(&thread_input->handles);
    render::BufferInfo* buffer_info = (render::BufferInfo*)data;
    Buffer<U8> buffer = buffer_info->buffer;
    AssetManager* asset_manager = asset_manager_get();

    // ~mgj: copy to staging and record copy command
    BufferAllocation staging_buffer_alloc = staging_buffer_create(buffer.size);
    VK_CHECK_RESULT(vmaCopyMemoryToAllocation(asset_manager->allocator, buffer.data,
                                              staging_buffer_alloc.allocation, 0, buffer.size));

    OS_MutexScopeW(asset_manager->buffer_mutex)
    {
        render::AssetItem<BufferUpload>* asset_item_buffer =
            asset_manager_item_get<BufferUpload>(handle);
        if (asset_item_buffer)
        {
            BufferUpload* asset_buffer = &asset_item_buffer->item;

            VkBufferCopy copy_region = {0};
            copy_region.size = buffer.size;
            vkCmdCopyBuffer((VkCommandBuffer)thread_input->cmd_buffer, staging_buffer_alloc.buffer,
                            asset_buffer->buffer_alloc.buffer, 1, &copy_region);

            asset_buffer->staging_buffer = staging_buffer_alloc;
        }
        else
        {
            DEBUG_LOG("buffer_loading_thread: Asset Item: %llu - Not Found", handle.u64);
            buffer_destroy(&staging_buffer_alloc);
        }
    }
}

g_internal void
texture_loading_thread(void* data, render::ThreadInput* thread_input)
{
    Assert(thread_input->handles.count == 1);
    render::Handle handle = render::handle_list_first_handle(&thread_input->handles);
    AssetManager* asset_manager = asset_manager_get();
    render::TextureUploadData* texture = (render::TextureUploadData*)data;
    Assert(texture);
    ImageAllocationResource image_allocation_resource =
        texture_upload_with_blitting((VkCommandBuffer)thread_input->cmd_buffer, texture);

    OS_MutexScopeW(asset_manager->texture_mutex)
    {
        render::AssetItem<Texture>* tex_asset = asset_manager_item_get<Texture>(handle);
        if (tex_asset)
        {
            tex_asset->item.image_resource = image_allocation_resource.image_resource;
            tex_asset->item.staging_allocation = image_allocation_resource.staging_buffer_alloc;
        }
        else
        {
            buffer_destroy(&image_allocation_resource.staging_buffer_alloc);
            image_resource_destroy(image_allocation_resource.image_resource);
            DEBUG_LOG("texture_loading_thread: Asset Item: %llu - Not Found", handle.u64);
        }
    }
}

static void
texture_loading_from_path_thread(void* data, render::ThreadInput* thread_input)
{
    Assert(thread_input->handles.count == 1);
    render::Handle handle = render::handle_list_first_handle(&thread_input->handles);
    ScratchScope scratch = ScratchScope(0, 0);
    render::TextureLoadingInfo* extra_info = (render::TextureLoadingInfo*)data;
    Buffer<U8> tex_buf = io::file_read(scratch.arena, extra_info->tex_path);
    Assert(tex_buf.size > 0 && "Texture file not found");
    B32 err = texture_gpu_upload_cmd_recording((VkCommandBuffer)thread_input->cmd_buffer, handle,
                                               tex_buf);
    if (err)
    {
        ERROR_LOG("Error when uploading texture - Error id: %d\n", err);
    }
}

static void
colormap_loading_thread(void* data, render::ThreadInput* thread_input)
{
    Assert(thread_input->handles.count == 1);
    render::Handle handle = render::handle_list_first_handle(&thread_input->handles);
    render::ColorMapLoadingInfo* colormap_info = (render::ColorMapLoadingInfo*)data;
    Buffer<U8> colormap_buf = {.data = (U8*)colormap_info->colormap_data,
                               .size = colormap_info->colormap_size};
    render::AssetItem<Texture>* asset = (render::AssetItem<Texture>*)handle.ptr;
    Assert(asset);
    colormap_texture_cmd_record((VkCommandBuffer)thread_input->cmd_buffer, &asset->item,
                                colormap_buf);
}

static void
thread_main(async::ThreadInfo thread_info, void* input)
{
    ScratchScope scratch = ScratchScope(0, 0);
    render::ThreadInput* thread_input = (render::ThreadInput*)input;

    AssetManager* asset_manager = asset_manager_get();

    // ~mgj: Record the command buffer
    thread_input->cmd_buffer = begin_command(
        asset_manager->device, &asset_manager->threaded_cmd_pools.data[thread_info.thread_id]);

    Assert(thread_input->handles.count > 0);
    Assert(thread_input->loading_func && thread_input->user_data);
    Assert(thread_input->done_loading_func != nullptr);
    thread_input->loading_func(thread_input->user_data, thread_input);

    VK_CHECK_RESULT(vkEndCommandBuffer((VkCommandBuffer)thread_input->cmd_buffer));

    // ~mgj: Enqueue the command buffer
    asset_cmd_queue_item_enqueue(thread_info.thread_id, thread_input);
}

// ~mgj: Descriptor Index Allocator
static void
descriptor_index_allocator_init(DescriptorIndexAllocator* alloc, Arena* arena, U32 max_index)
{
    alloc->free_indices = PushArray(arena, U32, max_index);
    alloc->free_count = 0;
    alloc->free_capacity = max_index;
    alloc->next_index = 0;
    alloc->max_index = max_index;
}

static U32
descriptor_index_allocate(DescriptorIndexAllocator* alloc)
{
    U32 index = 0;
    if (alloc->free_count > 0)
    {
        index = alloc->free_indices[--alloc->free_count];
    }
    else
    {
        Assert(alloc->next_index < alloc->max_index);
        index = alloc->next_index++;
    }
    return index;
}

static void
descriptor_index_free(DescriptorIndexAllocator* alloc, U32 index)
{
    Assert(alloc->free_count < alloc->free_capacity);
    alloc->free_indices[alloc->free_count++] = index;
}

// ~mgj: Deferred Deletion Queue Implementation
static void
deletion_queue_push(DeletionQueue* queue, render::Handle handle, U64 frames_in_flight)
{
    AssetManager* asset_manager = asset_manager_get();
    PendingDeletion* deletion = nullptr;

    OS_MutexScopeW(asset_manager->deletion_queue_mutex)
    {
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
        deletion->frame_to_delete = queue->frame_counter + frames_in_flight;

        DLLPushBack(queue->first, queue->last, deletion);
    }
    DEBUG_LOG("Asset ID: %llu - Queued for deferred deletion at frame %llu (current: %llu)",
              handle.u64, deletion->frame_to_delete, queue->frame_counter);
}

static void
deletion_queue_resource_free(PendingDeletion* deletion)
{
    switch (deletion->handle.type)
    {
        case render::HandleType::Buffer:
        {
            asset_manager_buffer_free(deletion->handle);
        }
        break;
        case render::HandleType::Texture:
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
    AssetManager* asset_manager = asset_manager_get();
    OS_MutexScopeW(asset_manager->deletion_queue_mutex)
    {
        for (PendingDeletion* deletion = queue->first;
             deletion && deletion->frame_to_delete <= queue->frame_counter;)
        {
            PendingDeletion* next = deletion->next;
            DLLRemove(queue->first, queue->last, deletion);
            DEBUG_LOG("Asset ID: %llu - Executing deferred deletion at frame %llu",
                      deletion->handle.u64, queue->frame_counter);

            deletion_queue_resource_free(deletion);
            SLLStackPush(queue->free_list, deletion);
            deletion = next;
        }
        queue->frame_counter++;
    }
}

static void
deletion_queue_delete_all(DeletionQueue* queue)
{
    AssetManager* asset_manager = asset_manager_get();
    OS_MutexScopeW(asset_manager->deletion_queue_mutex)
    {
        for (PendingDeletion* deletion = queue->first; deletion;)
        {
            PendingDeletion* next = deletion->next;
            DLLRemove(queue->first, queue->last, deletion);
            DEBUG_LOG("Asset ID: %llu - Force deleting from deletion queue", deletion->handle.u64);

            deletion_queue_resource_free(deletion);
            SLLStackPush(queue->free_list, deletion);
            deletion = next;
        }
    }
}

static AssetManager*
asset_manager_create(VkPhysicalDevice physical_device, VkDevice device, VkInstance instance,
                     VkQueue graphics_queue, U32 queue_family_index, async::Threads* threads,
                     U64 total_size_in_bytes)
{
    Arena* arena = arena_alloc();
    AssetManager* asset_manager = PushStruct(arena, AssetManager);
    asset_manager->arena = arena;
    asset_manager->total_size = total_size_in_bytes;
    asset_manager->work_queue = threads->msg_queue;
    asset_manager->threads = threads;
    asset_manager->threaded_cmd_pools =
        BufferAlloc<AssetManagerCommandPool>(arena, threads->thread_handles.size);

    // Store device references
    asset_manager->device = device;
    asset_manager->graphics_queue = graphics_queue;
    asset_manager->graphics_queue_family_index = queue_family_index;

    // Create VMA allocator
    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.physicalDevice = physical_device;
    allocatorInfo.device = device;
    allocatorInfo.instance = instance;
    vmaCreateAllocator(&allocatorInfo, &asset_manager->allocator);

    VkCommandPoolCreateInfo cmd_pool_info{};
    cmd_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cmd_pool_info.queueFamilyIndex = queue_family_index;
    cmd_pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;

    for (U32 i = 0; i < threads->thread_handles.size; i++)
    {
        asset_manager->threaded_cmd_pools.data[i].cmd_pool =
            command_pool_create(device, &cmd_pool_info);
        asset_manager->threaded_cmd_pools.data[i].mutex = OS_MutexAlloc();
    }

    asset_manager->cmd_wait_list = asset_manager_cmd_list_create();
    U32 cmd_queue_size = 10;
    asset_manager->cmd_queue =
        async::QueueInit<CmdQueueItem>(arena, cmd_queue_size, queue_family_index);

    descriptor_index_allocator_init(&asset_manager->descriptor_index_allocator, arena, 5000);

    asset_manager->deletion_queue = PushStruct(arena, DeletionQueue);
    asset_manager->deletion_queue_mutex = OS_RWMutexAlloc();

    // ~mgj: Mutex for asset operations (Textures and Buffers)
    asset_manager->texture_mutex = OS_RWMutexAlloc();
    asset_manager->buffer_mutex = OS_RWMutexAlloc();

    // Set global pointer
    g_asset_manager = asset_manager;

    return asset_manager;
}

static void
asset_manager_destroy(AssetManager* asset_manager)
{
    // 1. Drain all pending work while workers are still running normally.
    //    Workers finish current items and push to cmd_queue, which the drain
    //    loop submits to the GPU and waits for completion.
    asset_manager_cmd_list_destroy(asset_manager->cmd_wait_list);

    // 3. Clean up remaining resources (workers are stopped, no mutex needed)
    async::QueueDestroy(asset_manager->cmd_queue);
    deletion_queue_delete_all(asset_manager->deletion_queue);
    for (render::AssetItem<BufferUpload>* item = asset_manager->buffer_list.first; item != NULL;
         item = item->next)
    {
        buffer_destroy(&item->item.buffer_alloc);
        buffer_destroy(&item->item.staging_buffer);
    }

    for (render::AssetItem<Texture>* item = asset_manager->texture_list.first; item != NULL;
         item = item->next)
    {
        descriptor_index_free(&asset_manager->descriptor_index_allocator,
                              item->item.descriptor_set_idx);
        texture_destroy(&item->item);
    }

    for (U32 i = 0; i < asset_manager->threaded_cmd_pools.size; i++)
    {
        vkDestroyCommandPool(asset_manager->device,
                             asset_manager->threaded_cmd_pools.data[i].cmd_pool, 0);
        OS_MutexRelease(asset_manager->threaded_cmd_pools.data[i].mutex);
    }

    OS_MutexRelease(asset_manager->texture_mutex);
    OS_MutexRelease(asset_manager->buffer_mutex);

    vmaDestroyAllocator(asset_manager->allocator);

    arena_release(asset_manager->arena);
}

static void
asset_cmd_queue_item_enqueue(U32 thread_id, render::ThreadInput* thread_input)
{
    AssetManager* asset_manager = asset_manager_get();

    CmdQueueItem item = {.thread_input = thread_input, .thread_id = thread_id};
    DEBUG_LOG("Asset ID: %llu - Cmd Getting Queued",
              thread_input->handles.first ? thread_input->handles.first->handle.u64 : 0);
    Assert(thread_input->cmd_buffer != VK_NULL_HANDLE);
    async::QueuePush(asset_manager->cmd_queue, &item);
}

static void
asset_manager_execute_cmds()
{
    AssetManager* asset_manager = asset_manager_get();

    for (U32 i = 0; i < asset_manager->cmd_queue->queue_size; i++)
    {
        CmdQueueItem item;
        if (async::QueueTryRead(asset_manager->cmd_queue, &item))
        {
            VkSubmitInfo submit_info{};
            submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submit_info.commandBufferCount = 1;
            submit_info.pCommandBuffers = (VkCommandBuffer*)&item.thread_input->cmd_buffer;

            VkFenceCreateInfo fence_info{};
            fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            VK_CHECK_RESULT(
                vkCreateFence(asset_manager->device, &fence_info, nullptr, &item.fence));

            VK_CHECK_RESULT(
                vkQueueSubmit(asset_manager->graphics_queue, 1, &submit_info, item.fence));

            DEBUG_LOG("Asset ID: %llu - Submitted Command Buffer",
                      item.thread_input->handles.first
                          ? item.thread_input->handles.first->handle.u64
                          : 0);
            Assert(item.thread_input->done_loading_func);
            asset_manager_cmd_list_add(asset_manager->cmd_wait_list, item);
        }
    }
}

template <typename T>
static render::AssetItem<T>*
asset_manager_item_get(render::Handle handle)
{
    Assert(handle.u64 != 0);
    render::AssetItem<T>* asset_item = (render::AssetItem<T>*)handle.ptr;
    if (asset_item && asset_item->gen_id == handle.gen_id)
        return asset_item;

    if (!render::is_handle_zero(handle))
    {
        DEBUG_LOG("Asset ID: %llu - Not Found", handle.u64);
    }

    return 0;
}

static render::AssetItem<BufferUpload>*
asset_manager_buffer_item_get(render::Handle handle)
{
    AssetManager* asset_manager = asset_manager_get();
    render::AssetItem<BufferUpload>* asset_item_buffer = {};
    OS_MutexScopeR(asset_manager->buffer_mutex)
    {
        asset_item_buffer = asset_manager_item_get<BufferUpload>(handle);
    }
    return asset_item_buffer;
}

static render::AssetItem<Texture>*
asset_manager_texture_item_get(render::Handle handle)
{
    AssetManager* asset_manager = asset_manager_get();
    render::AssetItem<Texture>* asset_item_texture = {};
    OS_MutexScopeR(asset_manager->texture_mutex)
    {
        asset_item_texture = asset_manager_item_get<Texture>(handle);
    }
    return asset_item_texture;
}

// ~mgj: As it is right now, this function needs to be protected by a mutex like object as it is
// used by multiple threads.
template <typename T>
static render::Handle
asset_manager_item_create(render::AssetItemList<T>* list, render::AssetItemList<T>* free_list,
                          render::HandleType handle_type)
{
    AssetManager* asset_manager = asset_manager_get();
    Arena* arena = asset_manager->arena;

    render::AssetItem<T>* asset_item = free_list->first;
    if (asset_item)
    {
        DLLRemove(free_list->first, free_list->last, asset_item);
        MemoryZero(&asset_item->item, sizeof(T));
        free_list->count--;
        asset_item->gen_id++;
    }
    else
    {
        asset_item = PushStruct(arena, render::AssetItem<T>);
    }
    Assert(asset_item);

    DLLPushBack(list->first, list->last, asset_item);
    list->count++;

    Assert(asset_item);
    render::Handle handle = render::Handle(asset_item, asset_item->gen_id, handle_type);
    return handle;
}

static void
asset_manager_cmd_done_check()
{
    AssetManager* asset_manager = asset_manager_get();
    for (CmdQueueItem* cmd_queue_item = asset_manager->cmd_wait_list->list_first; cmd_queue_item;)
    {
        CmdQueueItem* next = cmd_queue_item->next;
        VkResult result = vkGetFenceStatus(asset_manager->device, cmd_queue_item->fence);
        if (result == VK_SUCCESS)
        {
            render::ThreadInput* thread_input = cmd_queue_item->thread_input;
            OS_MutexScope(asset_manager->threaded_cmd_pools.data[cmd_queue_item->thread_id].mutex)
            {
                vkFreeCommandBuffers(
                    asset_manager->device,
                    asset_manager->threaded_cmd_pools.data[cmd_queue_item->thread_id].cmd_pool, 1,
                    (VkCommandBuffer*)&thread_input->cmd_buffer);
            }

            Assert(thread_input->handles.count > 0);
            Assert(thread_input->loading_func != nullptr);
            Assert(thread_input->done_loading_func != nullptr);
            thread_input->done_loading_func(thread_input->handles);

            DEBUG_LOG("Asset: %llu - Finished loading", thread_input->handles.first->handle.u64);
            vkDestroyFence(asset_manager->device, cmd_queue_item->fence, 0);
            render::thread_input_destroy(cmd_queue_item->thread_input);
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
begin_command(VkDevice device, AssetManagerCommandPool* threaded_cmd_pool)
{
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = threaded_cmd_pool->cmd_pool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    OS_MutexScope(threaded_cmd_pool->mutex)
    {
        VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer));
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
    Arena* arena = arena_alloc();
    AssetManagerCmdList* cmd_list = PushStruct(arena, AssetManagerCmdList);
    cmd_list->arena = arena;
    return cmd_list;
}

static B32
asset_manager_has_pending_work(AssetManagerCmdList* cmd_wait_list)
{
    AssetManager* asset_manager = asset_manager_get();
    B32 cmd_queue_empty =
        asset_manager->cmd_queue->next_index == asset_manager->cmd_queue->fill_index;
    B32 work_queue_empty =
        asset_manager->work_queue->next_index == asset_manager->work_queue->fill_index;
    B32 workers_busy = asset_manager->threads->in_flight_count.load() > 0;
    return cmd_wait_list->list_first || !cmd_queue_empty || !work_queue_empty || workers_busy;
}

static void
asset_manager_cmd_list_destroy(AssetManagerCmdList* cmd_wait_list)
{
    AssetManager* asset_manager = asset_manager_get();
    while (asset_manager_has_pending_work(cmd_wait_list))
    {
        asset_manager_execute_cmds();
        vkDeviceWaitIdle(asset_manager->device);
        asset_manager_cmd_done_check();
    }
    arena_release(cmd_wait_list->arena);
}

static void
asset_manager_cmd_list_add(AssetManagerCmdList* cmd_list, CmdQueueItem item)
{
    CmdQueueItem* item_copy;
    if (cmd_list->free_list)
    {
        item_copy = cmd_list->free_list;
        SLLStackPop(cmd_list->free_list);
        MemoryZeroStruct(item_copy);
    }
    else
    {
        item_copy = PushStruct(cmd_list->arena, CmdQueueItem);
    }
    *item_copy = item;
    Assert(item.thread_input->done_loading_func);
    DLLPushBack(cmd_list->list_first, cmd_list->list_last, item_copy);
}

static void
asset_manager_cmd_list_item_remove(AssetManagerCmdList* cmd_list, CmdQueueItem* item)
{
    DLLRemove(cmd_list->list_first, cmd_list->list_last, item);
    MemoryZeroStruct(item);
    SLLStackPush(cmd_list->free_list, item);
}

template <typename T>
static void
asset_manager_item_free(render::AssetItem<T>* item, render::AssetItemList<T>* list,
                        render::AssetItemList<T>* free_list)
{
    item->is_loaded = false;
    item->gen_id++;
    DLLRemove(list->first, list->last, item);
    list->count--;
    DLLPushBack(free_list->first, free_list->last, item);
    free_list->count++;
}

static void
asset_manager_buffer_free(render::Handle handle)
{
    AssetManager* asset_manager = asset_manager_get();
    render::AssetItem<BufferUpload>* item = asset_manager_buffer_item_get(handle);
    if (item)
    {
        OS_MutexScopeW(asset_manager->buffer_mutex)
        {
            buffer_destroy(&item->item.buffer_alloc);
            buffer_destroy(&item->item.staging_buffer);
            asset_manager_item_free(item, &asset_manager->buffer_list,
                                    &asset_manager->buffer_free_list);
        }
    }
}

static void
asset_manager_texture_free(render::Handle handle)
{
    AssetManager* asset_manager = asset_manager_get();
    render::AssetItem<Texture>* item = asset_manager_texture_item_get(handle);
    if (item)
    {
        OS_MutexScopeW(asset_manager->texture_mutex)
        {
            descriptor_index_free(&asset_manager->descriptor_index_allocator,
                                  item->item.descriptor_set_idx);
            texture_destroy(&item->item);
            asset_manager_item_free(item, &asset_manager->texture_list,
                                    &asset_manager->texture_free_list);
        }
    }
}

//~mgj: Buffer Allocation Functions (VMA)
static BufferAllocation
buffer_allocation_create(VkDeviceSize size, VkBufferUsageFlags buffer_usage,
                         VmaAllocationCreateInfo vma_info)
{
    AssetManager* asset_manager = asset_manager_get();
    BufferAllocation buffer = {};

    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = buffer_usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VK_CHECK_RESULT(vmaCreateBuffer(asset_manager->allocator, &bufferInfo, &vma_info,
                                    &buffer.buffer, &buffer.allocation, nullptr));

    MEMORY_LOG("VMA Buffer Allocated: %p (size: %llu bytes, usage: 0x%x)", buffer.buffer,
               buffer.size, buffer_usage);

    return buffer;
}

static void
buffer_destroy(BufferAllocation* buffer_allocation)
{
    AssetManager* asset_manager = asset_manager_get();
    if (buffer_allocation->buffer != VK_NULL_HANDLE)
    {
        MEMORY_LOG("VMA Buffer Destroyed: %p (size: %llu bytes)", buffer_allocation->buffer,
                   buffer_allocation->size);
        vmaDestroyBuffer(asset_manager->allocator, buffer_allocation->buffer,
                         buffer_allocation->allocation);
        buffer_allocation->buffer = VK_NULL_HANDLE;
    }
}

static void
buffer_mapped_destroy(BufferAllocationMapped* mapped_buffer)
{
    buffer_destroy(&mapped_buffer->buffer_alloc);
    buffer_destroy(&mapped_buffer->staging_buffer_alloc);
    arena_release(mapped_buffer->arena);
}

static BufferAllocation
staging_buffer_create(VkDeviceSize size)
{
    VmaAllocationCreateInfo vma_staging_info = {0};
    vma_staging_info.usage = VMA_MEMORY_USAGE_AUTO;
    vma_staging_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    vma_staging_info.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    BufferAllocation staging_buffer =
        buffer_allocation_create(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, vma_staging_info);
    return staging_buffer;
}

static BufferAllocation
staging_buffer_mapped_create(VkDeviceSize size)
{
    AssetManager* asset_manager = asset_manager_get();
    VkBufferCreateInfo staging_buf_create_info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    staging_buf_create_info.size = size;
    staging_buf_create_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo staging_alloc_create_info = {};
    staging_alloc_create_info.usage = VMA_MEMORY_USAGE_AUTO;
    staging_alloc_create_info.flags =
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    BufferAllocation staging_buf_alloc = {};
    vmaCreateBuffer(asset_manager->allocator, &staging_buf_create_info, &staging_alloc_create_info,
                    &staging_buf_alloc.buffer, &staging_buf_alloc.allocation, nullptr);

    return staging_buf_alloc;
}

static void
buffer_alloc_create_or_resize(U32 total_buffer_byte_count, BufferAllocation* buffer_alloc,
                              VkBufferUsageFlags usage)
{
    if (total_buffer_byte_count > buffer_allocation_size_get(buffer_alloc))
    {
        buffer_destroy(buffer_alloc);

        VmaAllocationCreateInfo vma_info = {0};
        vma_info.usage = VMA_MEMORY_USAGE_AUTO;
        vma_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                         VMA_ALLOCATION_CREATE_MAPPED_BIT;
        vma_info.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;

        *buffer_alloc = buffer_allocation_create(total_buffer_byte_count, usage, vma_info);
    }
}

// inspiration:
// https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/usage_patterns.html
static BufferAllocationMapped
buffer_mapped_create(VkDeviceSize size, VkBufferUsageFlags buffer_usage)
{
    AssetManager* asset_manager = asset_manager_get();
    Arena* arena = arena_alloc();

    VmaAllocationCreateInfo vma_info = {0};
    vma_info.usage = VMA_MEMORY_USAGE_AUTO;
    vma_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                     VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT |
                     VMA_ALLOCATION_CREATE_MAPPED_BIT;

    BufferAllocation buffer =
        buffer_allocation_create(size, buffer_usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, vma_info);

    VkMemoryPropertyFlags mem_prop_flags;
    vmaGetAllocationMemoryProperties(asset_manager->allocator, buffer.allocation, &mem_prop_flags);

    VmaAllocationInfo alloc_info;
    vmaGetAllocationInfo(asset_manager->allocator, buffer.allocation, &alloc_info);

    BufferAllocationMapped mapped_buffer = {.buffer_alloc = buffer,
                                            .mapped_ptr = alloc_info.pMappedData,
                                            .mem_prop_flags = mem_prop_flags,
                                            .arena = arena};

    if (!mapped_buffer.mapped_ptr)
    {
        mapped_buffer.mapped_ptr = (void*)PushArray(mapped_buffer.arena, U8, size);

        vma_info = {0};
        vma_info.usage = VMA_MEMORY_USAGE_CPU_ONLY;
        vma_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                         VMA_ALLOCATION_CREATE_MAPPED_BIT;

        mapped_buffer.staging_buffer_alloc =
            buffer_allocation_create(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, vma_info);
    }

    return mapped_buffer;
}

static void
buffer_mapped_update(VkCommandBuffer cmd_buffer, BufferAllocationMapped mapped_buffer)
{
    AssetManager* asset_manager = asset_manager_get();
    U32 mapped_buffer_size = buffer_allocation_size_get(&mapped_buffer.buffer_alloc);

    if (mapped_buffer.mem_prop_flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
    {
        if ((mapped_buffer.mem_prop_flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0)
        {
            vmaFlushAllocation(asset_manager->allocator, mapped_buffer.buffer_alloc.allocation, 0,
                               mapped_buffer_size);
        }

        VkBufferMemoryBarrier buf_mem_barrier = {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        buf_mem_barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
        buf_mem_barrier.dstAccessMask = VK_ACCESS_UNIFORM_READ_BIT;
        buf_mem_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        buf_mem_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        buf_mem_barrier.buffer = mapped_buffer.buffer_alloc.buffer;
        buf_mem_barrier.offset = 0;
        buf_mem_barrier.size = VK_WHOLE_SIZE;

        vkCmdPipelineBarrier(cmd_buffer, VK_PIPELINE_STAGE_HOST_BIT,
                             VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, 0, 0, nullptr, 1,
                             &buf_mem_barrier, 0, nullptr);
    }
    else
    {
        if (vmaCopyMemoryToAllocation(asset_manager->allocator, mapped_buffer.mapped_ptr,
                                      mapped_buffer.staging_buffer_alloc.allocation, 0,
                                      mapped_buffer_size))
        {
            exit_with_error("BufferMappedUpdate: Could not copy data to staging buffer");
        }

        VkBufferMemoryBarrier bufMemBarrier = {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        bufMemBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
        bufMemBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        bufMemBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bufMemBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bufMemBarrier.buffer = mapped_buffer.staging_buffer_alloc.buffer;
        bufMemBarrier.offset = 0;
        bufMemBarrier.size = VK_WHOLE_SIZE;

        vkCmdPipelineBarrier(cmd_buffer, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, 1, &bufMemBarrier, 0, nullptr);

        VkBufferCopy bufCopy = {
            0,
            0,
            mapped_buffer_size,
        };

        vkCmdCopyBuffer(cmd_buffer, mapped_buffer.staging_buffer_alloc.buffer,
                        mapped_buffer.buffer_alloc.buffer, 1, &bufCopy);

        VkBufferMemoryBarrier bufMemBarrier2 = {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        bufMemBarrier2.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        bufMemBarrier2.dstAccessMask = VK_ACCESS_UNIFORM_READ_BIT;
        bufMemBarrier2.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bufMemBarrier2.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bufMemBarrier2.buffer = mapped_buffer.buffer_alloc.buffer;
        bufMemBarrier2.offset = 0;
        bufMemBarrier2.size = VK_WHOLE_SIZE;

        vkCmdPipelineBarrier(cmd_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, 0, 0, nullptr, 1, &bufMemBarrier2,
                             0, nullptr);
    }
}

static void
buffer_readback_create(VkDeviceSize size, VkBufferUsageFlags buffer_usage,
                       BufferReadback* out_buffer_readback)
{
    AssetManager* asset_manager = asset_manager_get();

    VkBufferCreateInfo bufCreateInfo = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufCreateInfo.size = size;
    bufCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | buffer_usage;

    VmaAllocationCreateInfo allocCreateInfo = {};
    allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocCreateInfo.flags =
        VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo alloc_info = {};
    VK_CHECK_RESULT(vmaCreateBuffer(asset_manager->allocator, &bufCreateInfo, &allocCreateInfo,
                                    &out_buffer_readback->buffer_alloc.buffer,
                                    &out_buffer_readback->buffer_alloc.allocation, &alloc_info));

    out_buffer_readback->mapped_ptr = alloc_info.pMappedData;
}

static void
buffer_readback_destroy(BufferReadback* out_buffer_readback)
{
    AssetManager* asset_manager = asset_manager_get();
    vmaDestroyBuffer(asset_manager->allocator, out_buffer_readback->buffer_alloc.buffer,
                     out_buffer_readback->buffer_alloc.allocation);
}

//~mgj: Image Allocation Functions (VMA)
static ImageAllocation
image_allocation_create(U32 width, U32 height, VkSampleCountFlagBits numSamples, VkFormat format,
                        VkImageTiling tiling, VkImageUsageFlags usage, U32 mipmap_level,
                        VmaAllocationCreateInfo vma_info, VkImageType image_type)
{
    AssetManager* asset_manager = asset_manager_get();
    ImageAllocation image_alloc = {0};
    VkExtent3D extent = {width, height, 1};

    VkImageCreateInfo image_create_info = {};
    image_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_create_info.imageType = image_type;
    image_create_info.extent = extent;
    image_create_info.mipLevels = mipmap_level;
    image_create_info.arrayLayers = 1;
    image_create_info.format = format;
    image_create_info.tiling = tiling;
    image_create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_create_info.usage = usage;
    image_create_info.samples = numSamples;
    image_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationInfo alloc_info;
    VK_CHECK_RESULT(vmaCreateImage(asset_manager->allocator, &image_create_info, &vma_info,
                                   &image_alloc.image, &image_alloc.allocation, &alloc_info))
    image_alloc.size = alloc_info.size;
    image_alloc.extent = extent;
    return image_alloc;
}

static void
image_allocation_destroy(ImageAllocation image_alloc)
{
    if (image_alloc.image)
    {
        AssetManager* asset_manager = asset_manager_get();
        vmaDestroyImage(asset_manager->allocator, image_alloc.image, image_alloc.allocation);
    }
}

static void
image_resource_destroy(ImageResource image)
{
    image_view_resource_destroy(image.image_view_resource);
    image_allocation_destroy(image.image_alloc);
}

} // namespace vulkan

namespace vulkan
{

// ~mgj: Global asset manager pointer
static AssetManager* g_asset_manager = nullptr;

static const char*
_allocation_name_get(VmaAllocator allocator, VmaAllocation allocation)
{
    if (allocation == 0)
    {
        return "<null>";
    }

    VmaAllocationInfo alloc_info = {};
    vmaGetAllocationInfo(allocator, allocation, &alloc_info);
    const char* result = alloc_info.pName ? alloc_info.pName : "<unnamed>";
    return result;
}

g_internal AssetManager*
asset_manager_get()
{
    Assert(g_asset_manager);
    return g_asset_manager;
}

static void
texture_destroy(TextureHandle* texture)
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
    const unsigned char ktx2_magic[12] = {0xab, 0x4b, 0x54, 0x58, 0x20, 0x32, 0x30, 0xbb, 0x0d, 0x0a, 0x1a, 0x0a};
    if (size >= ArrayCount(ktx2_magic) && MemoryMatch(ktx2_magic, buf, ArrayCount(ktx2_magic)))
    {
        result = true;
    }
    return result;
}

g_internal void
texture_ktx_cmd_record(VkCommandBuffer cmd, TextureHandle* tex, Buffer<U8> tex_buf)
{
    ScratchScope scratch = ScratchScope(0, 0);
    AssetManager* asset_manager = asset_manager_get();

    ktxTexture2* ktx_texture;
    ktx_error_code_e ktxresult = ktxTexture2_CreateFromMemory(tex_buf.data, tex_buf.size, NULL, &ktx_texture);

    if (ktxresult == KTX_SUCCESS)
    {
        U32 mip_levels = ktx_texture->numLevels;
        U32 base_width = ktx_texture->baseWidth;
        U32 base_height = ktx_texture->baseHeight;
        U32 base_depth = ktx_texture->baseDepth;

        Buffer<U32> mip_level_offsets = buffer_alloc<U32>(scratch.arena, mip_levels);
        for (U32 i = 0; i < ktx_texture->numLevels; ++i)
        {
            ktx_size_t offset;
            KTX_error_code ktx_error = ktxTexture_GetImageOffset((ktxTexture*)ktx_texture, i, 0, 0, &offset);
            if (ktx_error != KTX_SUCCESS)
            {
                exit_with_error("Failed to get image offset for mipmap level %u", i);
            }
            mip_level_offsets.data[i] = (U32)offset;
        }

        VkFormat vk_format = ktxTexture2_GetVkFormat(ktx_texture);

        BufferAllocation staging_allocation = _staging_buffer_mapped_create(ktx_texture->dataSize);
        VmaAllocationInfo staging_allocation_info;
        vmaGetAllocationInfo(asset_manager->allocator, staging_allocation.allocation, &staging_allocation_info);

        VmaAllocationCreateInfo vma_info = {.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
        ImageAllocation image_alloc =
            image_allocation_create(base_width, base_height, VK_SAMPLE_COUNT_1_BIT, vk_format, VK_IMAGE_TILING_OPTIMAL,
                                    VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, mip_levels, vma_info, "texture_ktx image");

        ImageViewResource image_view_resource = image_view_resource_create(asset_manager->device, image_alloc.image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT, mip_levels);

        KTX_error_code ktx_error = ktxTexture_LoadImageData((ktxTexture*)ktx_texture, (U8*)staging_allocation_info.pMappedData, ktx_texture->dataSize);
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
                              .imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = level, .baseArrayLayer = 0, .layerCount = 1},
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
            .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0, .levelCount = mip_levels, .baseArrayLayer = 0, .layerCount = 1},
        };
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);
        vkCmdCopyBufferToImage(cmd, staging_allocation.buffer, image_alloc.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mip_levels, regions);

        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);

        tex->image_resource = ImageResource(image_alloc, image_view_resource);
        tex->staging_allocation = staging_allocation;

        ktxTexture_Destroy((ktxTexture*)ktx_texture);
    }
    Assert(ktxresult == KTX_SUCCESS);
}

g_internal void
colormap_texture_cmd_record(VkCommandBuffer cmd, TextureHandle* tex, Buffer<U8> buf)
{
    ScratchScope scratch = ScratchScope(0, 0);
    AssetManager* asset_manager = asset_manager_get();
    U32 sizeof_rgb = 3;
    U32 sizeof_f32 = sizeof(F32);
    U32 colormap_size = buf.size / sizeof_rgb / sizeof_f32;
    U32 staging_buffer_size = colormap_size * 4 * (U32)sizeof(F32);
    AssertAlways(colormap_size == 256);
    VkFormat vk_format = VK_FORMAT_R32G32B32A32_SFLOAT;

    BufferAllocation staging_allocation = _staging_buffer_mapped_create(staging_buffer_size);
    VmaAllocationInfo staging_allocation_info;
    vmaGetAllocationInfo(asset_manager->allocator, staging_allocation.allocation, &staging_allocation_info);

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
    ImageAllocation image_alloc =
        image_allocation_create(colormap_size, 1, VK_SAMPLE_COUNT_1_BIT, vk_format, VK_IMAGE_TILING_OPTIMAL,
                                VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, mip_levels, vma_info, "colormap_texture_image", VK_IMAGE_TYPE_2D);

    ImageViewResource image_view_resource = image_view_resource_create(asset_manager->device, image_alloc.image, vk_format, VK_IMAGE_ASPECT_COLOR_BIT, mip_levels, VK_IMAGE_VIEW_TYPE_2D);

    VkBufferImageCopy* region = PushStruct(scratch.arena, VkBufferImageCopy);
    *region = {.bufferOffset = 0,
               .bufferRowLength = 0,
               .bufferImageHeight = 0,
               .imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1},
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
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0, .levelCount = mip_levels, .baseArrayLayer = 0, .layerCount = 1},
    };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);
    vkCmdCopyBufferToImage(cmd, staging_allocation.buffer, image_alloc.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mip_levels, region);

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);

    tex->image_resource = ImageResource(image_alloc, image_view_resource);
    tex->staging_allocation = staging_allocation;
}

g_internal ImageAllocationResource
texture_upload_with_blitting(VkCommandBuffer cmd, render::TextureUploadData* data)
{
    AssetManager* asset_manager = asset_manager_get();
    U32 mip_levels = floor(log2f(Max(data->width, data->height))) + 1;
    VkFormat vk_format = VK_FORMAT_R8G8B8A8_SRGB;

    BufferAllocation staging_allocation = _staging_buffer_mapped_create(data->data_byte_size);
    // upload base mip level

    VmaAllocationCreateInfo vma_info = {.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
    ImageAllocation image_alloc =
        image_allocation_create(data->width, data->height, VK_SAMPLE_COUNT_1_BIT, vk_format, VK_IMAGE_TILING_OPTIMAL,
                                VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, mip_levels, vma_info, "texture_upload_with_blitting image");

    // TODO: Implement error handling
    VK_CHECK_RESULT(vmaCopyMemoryToAllocation(asset_manager->allocator, data->data, staging_allocation.allocation, 0, data->data_byte_size));

    ImageViewResource image_view_resource = image_view_resource_create(asset_manager->device, image_alloc.image, vk_format, VK_IMAGE_ASPECT_COLOR_BIT, mip_levels);

    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image_alloc.image,
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0, .levelCount = mip_levels, .baseArrayLayer = 0, .layerCount = 1},
    };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);

    VkBufferImageCopy region;
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1};
    region.imageOffset = {.x = 0, .y = 0, .z = 0};
    region.imageExtent = {.width = (U32)data->width, .height = (U32)data->height, .depth = 1};

    vkCmdCopyBufferToImage(cmd, staging_allocation.buffer, image_alloc.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    S32 dst_img_width = data->width;
    S32 dst_img_height = data->height;
    for (U32 blit_lvl = 1; blit_lvl < mip_levels; blit_lvl++)
    {
        S32 src_img_width = dst_img_width;
        S32 src_img_height = dst_img_height;
        dst_img_width = Max((src_img_width >> 1), 1);
        dst_img_height = Max((src_img_height >> 1), 1);

        blit_transition_image(cmd, image_alloc.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, blit_lvl - 1);

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
        blit_transition_image(cmd, image_alloc.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, blit_lvl - 1);
    }
    blit_transition_image(cmd, image_alloc.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, mip_levels - 1);

    ImageAllocationResource image_allocation_resource = ImageAllocationResource(ImageResource(image_alloc, image_view_resource), staging_allocation);
    return image_allocation_resource;
}

g_internal B32
texture_cmd_record_with_stb(VkCommandBuffer cmd, TextureHandle* tex, Buffer<U8> tex_buf)
{
    S32 width;
    S32 height;
    S32 desired_num_channels = STBI_rgb_alpha;
    U8* image_data = stbi_load_from_memory(tex_buf.data, tex_buf.size, &width, &height, NULL, desired_num_channels);
    B32 err = false;
    err = !image_data;

    Assert(image_data);
    if (image_data)
    {
        constexpr U32 bytes_per_pixel = 1;
        render::TextureUploadData tex_data = render::TextureUploadData::init(image_data, (U32)width, (U32)height, (U32)desired_num_channels, bytes_per_pixel);
        ImageAllocationResource image_allocation_resource = texture_upload_with_blitting(cmd, &tex_data);
        tex->image_resource = image_allocation_resource.image_resource;
        tex->staging_allocation = image_allocation_resource.staging_buffer_alloc;
    };
    return err;
}

g_internal B32
texture_gpu_upload_cmd_recording(VkCommandBuffer cmd, render::Handle tex_handle, Buffer<U8> tex_buf)
{
    render::AssetItem<TextureHandle>* tex_asset = vulkan::asset_manager_texture_item_get(tex_handle);
    TextureHandle* tex = &tex_asset->item;
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

static void
buffer_loading_thread(void* data, render::ThreadWorkerCmdCtx* thread_input)
{
    Assert(thread_input->handles.count == 1);
    render::Handle handle = render::handle_list_first_handle(&thread_input->handles);
    render::BufferInfo* buffer_info = (render::BufferInfo*)data;
    Buffer<U8> buffer = buffer_info->buffer;
    AssetManager* asset_manager = asset_manager_get();

    // ~mgj: copy to staging and record copy command
    BufferAllocation staging_buffer_alloc = _staging_buffer_create(buffer.size);
    VK_CHECK_RESULT(vmaCopyMemoryToAllocation(asset_manager->allocator, buffer.data, staging_buffer_alloc.allocation, 0, buffer.size));

    os_mutex_scope_w(asset_manager->buffer_mutex)
    {
        render::AssetItem<BufferHandle>* asset_item_buffer = asset_manager_item_get<BufferHandle>(handle);
        if (asset_item_buffer)
        {
            BufferHandle* asset_buffer = &asset_item_buffer->item;

            VkBufferCopy copy_region = {0};
            copy_region.size = buffer.size;
            vkCmdCopyBuffer((VkCommandBuffer)thread_input->cmd_buffer, staging_buffer_alloc.buffer, asset_buffer->buffer_alloc.buffer, 1, &copy_region);

            asset_buffer->staging_buffer = staging_buffer_alloc;
            asset_buffer->item_byte_size = buffer_info->type_size;
            asset_buffer->elem_count = buffer_info->elem_count;
        }
        else
        {
            buffer_destroy(&staging_buffer_alloc);
        }
    }
}

static void
texture_loading_from_path_thread(void* data, render::ThreadWorkerCmdCtx* thread_input)
{
    Assert(thread_input->handles.count == 1);
    render::Handle handle = render::handle_list_first_handle(&thread_input->handles);
    ScratchScope scratch = ScratchScope(0, 0);
    render::TextureLoadingInfo* extra_info = (render::TextureLoadingInfo*)data;
    Buffer<U8> tex_buf = io::file_read(scratch.arena, extra_info->tex_path);
    Assert(tex_buf.size > 0 && "Texture file not found");
    B32 err = texture_gpu_upload_cmd_recording((VkCommandBuffer)thread_input->cmd_buffer, handle, tex_buf);
    if (err)
    {
        ERROR_LOG("Error when uploading texture - Error id: %d\n", err);
    }
}

static void
colormap_loading_thread(void* data, render::ThreadWorkerCmdCtx* thread_ctx)
{
    Assert(thread_ctx->handles.count == 1);
    render::Handle handle = render::handle_list_first_handle(&thread_ctx->handles);
    render::ColorMapLoadingInfo* colormap_info = (render::ColorMapLoadingInfo*)data;
    colormap_loading_thread(handle, colormap_info, thread_ctx);
}

g_internal void
colormap_loading_thread(render::Handle handle, render::ColorMapLoadingInfo* colormap_info, render::ThreadWorkerCmdCtx* thread_input)
{
    Buffer<U8> colormap_buf = {.data = (U8*)colormap_info->colormap_data, .size = colormap_info->colormap_size};
    render::AssetItem<TextureHandle>* asset = (render::AssetItem<TextureHandle>*)handle.ptr;
    Assert(asset);
    colormap_texture_cmd_record((VkCommandBuffer)thread_input->cmd_buffer, &asset->item, colormap_buf);
}

static async::WorkerResult
thread_main(async::ThreadInfo thread_info, async::WorkerData input)
{
    ScratchScope scratch = ScratchScope(0, 0);
    render::ThreadWorkerCmdCtx* thread_cmd_ctx = (render::ThreadWorkerCmdCtx*)input;

    AssetManager* asset_manager = asset_manager_get();

    // ~mgj: Record the command buffer
    AssetManagerCommandPool thread_cmd_pool = asset_manager_cmd_pool_get(asset_manager, thread_info.thread_id);
    thread_cmd_ctx->cmd_buffer = begin_command(asset_manager->device, &thread_cmd_pool);

    Assert(thread_cmd_ctx->handles.count > 0);
    Assert(thread_cmd_ctx->loading_func || thread_cmd_ctx->user_data);
    thread_cmd_ctx->loading_func(thread_cmd_ctx->user_data, thread_cmd_ctx);

    end_command(&thread_cmd_pool, (VkCommandBuffer)thread_cmd_ctx->cmd_buffer);

    // ~mgj: Enqueue the command buffer
    asset_cmd_queue_item_enqueue(thread_info.thread_id, thread_cmd_ctx);
    return {};
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
        AssertAlways(alloc->next_index < alloc->max_index);
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
deletion_queue_push(render::Handle handle)
{
    AssetManager* asset_manager = asset_manager_get();
    {
        U64 current_queue_idx = asset_manager->deletion_queue_idx;
        DeletionQueue* queue = &asset_manager->deletion_queues[current_queue_idx];

        PendingDeletion* deletion = asset_manager->deletion_queue_free_list;
        if (deletion)
        {
            SLLStackPop(asset_manager->deletion_queue_free_list);
            MemoryZero(deletion, sizeof(*deletion));
            asset_manager->deletion_queue_free_list_count--;
        }
        else
        {
            deletion = PushStruct(asset_manager->arena, PendingDeletion);
        }

        deletion->handle = handle;

        Debug_Asset_Push_ScheduleDeletion(handle);

        DLLPushBack(queue->first, queue->last, deletion);
        queue->list_count++;
    }
}

static void
deletion_queue_empty(DeletionQueue* queue)
{
    AssetManager* asset_manager = asset_manager_get();
    {
        while (queue->first)
        {
            PendingDeletion* deletion = queue->first;
            DLLRemove(queue->first, queue->last, deletion);
            queue->list_count--;

            asset_manager_handle_free(deletion->handle);
            SLLStackPush(asset_manager->deletion_queue_free_list, deletion);
            asset_manager->deletion_queue_free_list_count++;
        }
    }
}

g_internal void
deletion_queue_empty_next()
{
    AssetManager* asset_manager = asset_manager_get();
    U64 next_frame = (asset_manager->deletion_queue_idx + 1) % ArrayCount(asset_manager->deletion_queues);
    DeletionQueue* queue = &asset_manager->deletion_queues[next_frame];
    deletion_queue_empty(queue);

    asset_manager->deletion_queue_idx = next_frame;
}

static void
deletion_queue_empty_all()
{
    AssetManager* asset_manager = asset_manager_get();
    for (U32 i = 0; i < ArrayCount(asset_manager->deletion_queues); i++)
    {
        DeletionQueue* queue = &asset_manager->deletion_queues[i];
        deletion_queue_empty(queue);
    }
}

static AssetManager*
asset_manager_create(VkPhysicalDevice physical_device, VkDevice device, VkInstance instance, VkQueue graphics_queue, U32 queue_family_index, async::ThreadPool* threads, U64 total_size_in_bytes,
                     VkDescriptorPool desc_pool)
{
    Arena* arena = arena_alloc();
    AssetManager* asset_manager = PushStruct(arena, AssetManager);
    asset_manager->arena = arena;
    asset_manager->total_size = total_size_in_bytes;
    asset_manager->threads = threads;
    asset_manager->threaded_cmd_pools = buffer_alloc<AssetManagerCommandPool>(arena, threads->thread_handles.size);
    asset_manager->descriptor_pool = desc_pool;

    // Store device references
    asset_manager->device = device;
    asset_manager->graphics_queue = graphics_queue;
    asset_manager->graphics_queue_family_index = queue_family_index;

    // Create VMA allocator
    VmaVulkanFunctions vulkan_functions = {};
    vulkan_functions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    vulkan_functions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;

    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.flags = VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
    allocatorInfo.physicalDevice = physical_device;
    allocatorInfo.device = device;
    allocatorInfo.instance = instance;
    allocatorInfo.pVulkanFunctions = &vulkan_functions;
    VK_CHECK_RESULT(vmaCreateAllocator(&allocatorInfo, &asset_manager->allocator));

    VkCommandPoolCreateInfo cmd_pool_info{};
    cmd_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cmd_pool_info.queueFamilyIndex = queue_family_index;
    cmd_pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;

    for (U32 i = 0; i < threads->thread_handles.size; i++)
    {
        asset_manager->threaded_cmd_pools.data[i].cmd_pool = command_pool_create(device, &cmd_pool_info);
        asset_manager->threaded_cmd_pools.data[i].mutex = OS_MutexAlloc();
    }
    asset_manager->main_thread_cmd_pool = command_pool_create(device, &cmd_pool_info);

    asset_manager->cmd_wait_list = asset_manager_cmd_list_create();
    asset_manager->cmd_queue = asset_manager_cmd_queue_create();

    descriptor_index_allocator_init(&asset_manager->descriptor_index_allocator, arena, 10000);

    // ~mgj: Mutex for asset operations (Textures and Buffers)
    asset_manager->texture_mutex = os_rw_mutex_alloc();
    asset_manager->buffer_mutex = os_rw_mutex_alloc();
    asset_manager->arena_mutex = OS_MutexAlloc();

    // Set global pointer
    g_asset_manager = asset_manager;

    return asset_manager;
}

static void
_asset_manager_live_resources_destroy(AssetManager* asset_manager)
{
    for (render::AssetItem<BufferHandle>* item = asset_manager->buffer_list.first; item != NULL; item = item->next)
    {
        const char* buffer_name = _allocation_name_get(asset_manager->allocator, item->item.buffer_alloc.allocation);
        const char* staging_name = _allocation_name_get(asset_manager->allocator, item->item.staging_buffer.allocation);
        DEBUG_LOG("Buffer Not Destroyed: gen_id=%llu, name=%s, staging=%s", (U64)item->gen_id, buffer_name, staging_name);
        buffer_destroy(&item->item.buffer_alloc);
        buffer_destroy(&item->item.staging_buffer);
    }

    for (render::AssetItem<TextureHandle>* item = asset_manager->texture_list.first; item != NULL; item = item->next)
    {
        const char* image_name = _allocation_name_get(asset_manager->allocator, item->item.image_resource.image_alloc.allocation);
        const char* staging_name = _allocation_name_get(asset_manager->allocator, item->item.staging_allocation.allocation);
        DEBUG_LOG("Texture Not Destroyed: gen_id=%llu, image=%s, staging=%s", (U64)item->gen_id, image_name, staging_name);
        descriptor_index_free(&asset_manager->descriptor_index_allocator, item->item.descriptor_set_idx);
        texture_destroy(&item->item);
    }
}

static void
asset_manager_destroy(AssetManager* asset_manager)
{
    asset_manager->shutting_down = true;

    // 1. Drain all pending work while workers are still running normally.
    //    Workers finish current items and push to cmd_queue, which the drain
    //    loop submits to the GPU and waits for completion.
    asset_manager_cmd_list_destroy(asset_manager->cmd_wait_list);

    // 3. Clean up remaining resources (workers are stopped, no mutex needed)
    asset_manager_cmd_queue_destroy(asset_manager->cmd_queue);
    deletion_queue_empty_all();

    _asset_manager_live_resources_destroy(asset_manager);

#if BUILD_DEBUG
    {
        char* vma_json = nullptr;
        vmaBuildStatsString(asset_manager->allocator, &vma_json, VK_TRUE);
        DEBUG_LOG("VMA live allocations after asset shutdown:\n%s", vma_json);
        vmaFreeStatsString(asset_manager->allocator, vma_json);
    }
#endif

    for (U32 i = 0; i < asset_manager->threaded_cmd_pools.size; i++)
    {
        vkDestroyCommandPool(asset_manager->device, asset_manager->threaded_cmd_pools.data[i].cmd_pool, 0);
        OS_MutexRelease(asset_manager->threaded_cmd_pools.data[i].mutex);
    }
    vkDestroyCommandPool(asset_manager->device, asset_manager->main_thread_cmd_pool, 0);

    OS_MutexRelease(asset_manager->texture_mutex);
    OS_MutexRelease(asset_manager->buffer_mutex);
    OS_MutexRelease(asset_manager->arena_mutex);

    vmaDestroyAllocator(asset_manager->allocator);
    g_asset_manager = 0;

    arena_release(asset_manager->arena);
}

static void
asset_cmd_queue_item_enqueue(U32 thread_id, render::ThreadWorkerCmdCtx* thread_input)
{
    AssetManager* asset_manager = asset_manager_get();

    CmdQueueItem item = {.thread_input = thread_input, .thread_id = thread_id};
    for (render::HandleNode* node = thread_input->handles.first; node; node = node->next)
    {
        Debug_Asset_Push_Queued(node->handle);
    }
    Assert(thread_input->cmd_buffer != VK_NULL_HANDLE);
    asset_manager_cmd_queue_enqueue(asset_manager->cmd_queue, item);
}

static void
asset_manager_execute_cmds()
{
    AssetManager* asset_manager = asset_manager_get();

    for (;;)
    {
        CmdQueueItem item;
        if (!asset_manager_cmd_queue_try_dequeue(asset_manager->cmd_queue, &item))
        {
            break;
        }

        VkSubmitInfo submit_info{};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = (VkCommandBuffer*)&item.thread_input->cmd_buffer;

        VkFenceCreateInfo fence_info{};
        fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        VK_CHECK_RESULT(vkCreateFence(asset_manager->device, &fence_info, nullptr, &item.fence));

        VK_CHECK_RESULT(vkQueueSubmit(asset_manager->graphics_queue, 1, &submit_info, item.fence));

        for (render::HandleNode* node = item.thread_input->handles.first; node; node = node->next)
        {
            Debug_Asset_Push_GpuSubmitted(node->handle);
        }
        asset_manager_cmd_list_add(asset_manager->cmd_wait_list, item);
    }
}

template <typename T>
static render::AssetItem<T>*
asset_manager_item_get(render::Handle handle)
{
    Assert(handle.u64 != 0);
    render::AssetItem<T>* asset_item = (render::AssetItem<T>*)handle.ptr;
    if (!asset_item)
    {
        Debug_Asset_Push_NotFound(handle);
        return 0;
    }

    if (asset_item->gen_id != handle.gen_id)
    {
        Debug_Asset_Push_WrongGenId(handle);
        return 0;
    }

    if (asset_item->type != handle.type)
    {
        Debug_Asset_Push_WrongHandleType(handle);
        return 0;
    }

    return asset_item;
}

static render::AssetItem<BufferHandle>*
asset_manager_buffer_item_get(render::Handle handle)
{
    AssetManager* asset_manager = asset_manager_get();
    render::AssetItem<BufferHandle>* asset_item_buffer = {};
    os_mutex_scope_r(asset_manager->buffer_mutex)
    {
        asset_item_buffer = asset_manager_item_get<BufferHandle>(handle);
    }
    return asset_item_buffer;
}

static render::AssetItem<TextureHandle>*
asset_manager_texture_item_get(render::Handle handle)
{
    AssetManager* asset_manager = asset_manager_get();
    render::AssetItem<TextureHandle>* asset_item_texture = {};
    os_mutex_scope_r(asset_manager->texture_mutex)
    {
        asset_item_texture = asset_manager_item_get<TextureHandle>(handle);
    }
    return asset_item_texture;
}

// ~mgj: Callers must hold their per-type mutex. Arena pushes are additionally protected by
// arena_mutex to prevent races across different asset types on the shared arena.
template <typename T>
static render::Handle
asset_manager_item_create(render::AssetItemList<T>* list, render::AssetItemList<T>* free_list, render::HandleType handle_type)
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
        os_mutex_scope(asset_manager->arena_mutex)
        {
            asset_item = PushStruct(arena, render::AssetItem<T>);
        }
    }
    Assert(asset_item);
    asset_item->type = handle_type;

    DLLPushBack(list->first, list->last, asset_item);
    list->count++;

    Assert(asset_item);
    render::Handle handle = render::Handle(asset_item, asset_item->gen_id, handle_type);
    Debug_Asset_Push_Created(handle);
    return handle;
}

g_internal render::Handle
asset_manager_buffer_allocation_create(render::ThreadWorkerCmdCtx* thread_ctx, render::BufferInfo* buffer_info, VmaAllocationCreateInfo vma_info)
{
    AssetManager* asset_manager = asset_manager_get();
    render::Handle handle = render::Handle::buffer_handle_create((render::BufferType)buffer_info->buffer_type);
    render::handle_list_push(thread_ctx, handle);

    VkBufferUsageFlags usage_flags = {};
    U32 buffer_type = buffer_info->buffer_type;
    if (buffer_type & render::BufferType_Vertex)
        usage_flags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (buffer_type & render::BufferType_Index)
        usage_flags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (buffer_type & render::BufferType_Uniform)
        usage_flags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if (buffer_type & render::BufferType_StorageBuffer)
        usage_flags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    Assert(usage_flags != 0);
    vulkan::BufferAllocation buffer_alloc = _buffer_allocation_create(buffer_info->buffer.size, usage_flags | VK_BUFFER_USAGE_TRANSFER_DST_BIT, &vma_info, nullptr);

    render::AssetItem<vulkan::BufferHandle>* asset_item = vulkan::asset_manager_buffer_item_get(handle);
    vulkan::BufferHandle* asset_buffer = (vulkan::BufferHandle*)&asset_item->item;
    asset_buffer->buffer_alloc = buffer_alloc;
    asset_buffer->item_byte_size = buffer_info->type_size;
    asset_buffer->elem_count = buffer_info->elem_count;

    VkMemoryPropertyFlags mem_prop_flags;
    vmaGetAllocationMemoryProperties(asset_manager->allocator, buffer_alloc.allocation, &mem_prop_flags);
    asset_buffer->mem_prop_flags = mem_prop_flags;

    return handle;
}

g_internal vulkan::BufferAllocation
asset_manager_buffer_from_staging(VkCommandBuffer cmd_buffer, render::BufferInfo* buffer_info, VkBuffer dest_buffer)
{
    vulkan::BufferAllocation staging_alloc = vulkan::_staging_buffer_create(buffer_info->buffer.size);
    vulkan::AssetManager* asset_manager = vulkan::asset_manager_get();
    VK_CHECK_RESULT(vmaCopyMemoryToAllocation(asset_manager->allocator, buffer_info->buffer.data, staging_alloc.allocation, 0, buffer_info->buffer.size));
    VkBufferCopy copy_region = {0};
    copy_region.size = buffer_info->buffer.size;
    vkCmdCopyBuffer(cmd_buffer, staging_alloc.buffer, dest_buffer, 1, &copy_region);
    return staging_alloc;
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
            render::ThreadWorkerCmdCtx* thread_input = cmd_queue_item->thread_input;
            AssetManagerCommandPool cmd_pool = asset_manager_cmd_pool_get(asset_manager, cmd_queue_item->thread_id);
            if (OS_HandleMatch(cmd_pool.mutex, OS_HandleIsZero()))
            {
                vkFreeCommandBuffers(asset_manager->device, cmd_pool.cmd_pool, 1, (VkCommandBuffer*)&thread_input->cmd_buffer);
            }
            else
            {
                os_mutex_scope(cmd_pool.mutex)
                {
                    vkFreeCommandBuffers(asset_manager->device, cmd_pool.cmd_pool, 1, (VkCommandBuffer*)&thread_input->cmd_buffer);
                }
            }

            Assert(thread_input->handles.count > 0);
            render::handle_done_loading(thread_input->handles);

            for (render::HandleNode* node = thread_input->handles.first; node; node = node->next)
            {
                if (node->work_on_gpu_done)
                {
                    Debug_Asset_Push_GpuSubmissionDone(node->handle);
                }
            }
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

static AssetManagerCommandPool
asset_manager_cmd_pool_get(AssetManager* asset_manager, U32 thread_id)
{
    AssetManagerCommandPool result = {};
    if (thread_id == max_U32)
    {
        result.cmd_pool = asset_manager->main_thread_cmd_pool;
    }
    else
    {
        result = asset_manager->threaded_cmd_pools.data[thread_id];
    }
    return result;
}

static VkCommandBuffer
begin_command(VkDevice device, AssetManagerCommandPool* threaded_cmd_pool)
{
    AssertAlways(threaded_cmd_pool->cmd_pool);
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = threaded_cmd_pool->cmd_pool;
    allocInfo.commandBufferCount = 1;

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VkCommandBuffer commandBuffer;
    if (OS_HandleMatch(threaded_cmd_pool->mutex, OS_HandleIsZero()))
    {
        VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer));
        VK_CHECK_RESULT(vkBeginCommandBuffer(commandBuffer, &beginInfo));
    }
    else
    {
        os_mutex_scope(threaded_cmd_pool->mutex)
        {
            VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer));
            VK_CHECK_RESULT(vkBeginCommandBuffer(commandBuffer, &beginInfo));
        }
    }

    return commandBuffer;
}

static void
end_command(AssetManagerCommandPool* threaded_cmd_pool, VkCommandBuffer command_buffer)
{
    if (OS_HandleMatch(threaded_cmd_pool->mutex, OS_HandleIsZero()))
    {
        VK_CHECK_RESULT(vkEndCommandBuffer(command_buffer));
    }
    else
    {
        os_mutex_scope(threaded_cmd_pool->mutex)
        {
            VK_CHECK_RESULT(vkEndCommandBuffer(command_buffer));
        }
    }
}

static AssetManagerCmdQueue*
asset_manager_cmd_queue_create()
{
    Arena* arena = arena_alloc();
    AssetManagerCmdQueue* cmd_queue = PushStruct(arena, AssetManagerCmdQueue);
    cmd_queue->arena = arena;
    cmd_queue->mutex = OS_MutexAlloc();
    return cmd_queue;
}

static void
asset_manager_cmd_queue_destroy(AssetManagerCmdQueue* cmd_queue)
{
    if (cmd_queue)
    {
        OS_MutexRelease(cmd_queue->mutex);
        arena_release(cmd_queue->arena);
    }
}

static void
asset_manager_cmd_queue_enqueue(AssetManagerCmdQueue* cmd_queue, CmdQueueItem item)
{
    os_mutex_scope(cmd_queue->mutex)
    {
        CmdQueueItem* item_copy = 0;
        if (cmd_queue->free_list)
        {
            item_copy = cmd_queue->free_list;
            SLLStackPop(cmd_queue->free_list);
            MemoryZeroStruct(item_copy);
        }
        else
        {
            item_copy = PushStruct(cmd_queue->arena, CmdQueueItem);
        }

        *item_copy = item;
        DLLPushBack(cmd_queue->first, cmd_queue->last, item_copy);
        cmd_queue->count++;
    }
}

static B32
asset_manager_cmd_queue_try_dequeue(AssetManagerCmdQueue* cmd_queue, CmdQueueItem* item)
{
    B32 has_item = false;
    os_mutex_scope(cmd_queue->mutex)
    {
        CmdQueueItem* item_node = cmd_queue->first;
        if (item_node)
        {
            *item = *item_node;
            DLLRemove(cmd_queue->first, cmd_queue->last, item_node);
            cmd_queue->count--;
            MemoryZeroStruct(item_node);
            SLLStackPush(cmd_queue->free_list, item_node);
            has_item = true;
        }
    }
    return has_item;
}

static B32
asset_manager_cmd_queue_has_pending_work(AssetManagerCmdQueue* cmd_queue)
{
    B32 has_pending_work = false;
    os_mutex_scope(cmd_queue->mutex)
    {
        has_pending_work = cmd_queue->count > 0;
    }
    return has_pending_work;
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
    B32 cmd_queue_has_pending_work = asset_manager_cmd_queue_has_pending_work(asset_manager->cmd_queue);
    B32 workers_busy = async::thread_pool_has_pending_work(asset_manager->threads);
    return cmd_wait_list->list_first || cmd_queue_has_pending_work || workers_busy;
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
asset_manager_item_free(render::AssetItem<T>* item, render::AssetItemList<T>* list, render::AssetItemList<T>* free_list)
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
    render::AssetItem<BufferHandle>* item = asset_manager_buffer_item_get(handle);
    if (item)
    {
        os_mutex_scope_w(asset_manager->buffer_mutex)
        {
            buffer_destroy(&item->item.buffer_alloc);
            buffer_destroy(&item->item.staging_buffer);
            asset_manager_item_free(item, &asset_manager->buffer_list, &asset_manager->buffer_free_list);
        }
    }
}

static void
asset_manager_texture_free(render::Handle handle)
{
    vulkan::Context* vk_ctx = ctx_get();
    AssetManager* asset_manager = vk_ctx->asset_manager;
    render::AssetItem<TextureHandle>* item = asset_manager_texture_item_get(handle);
    render::AssetItem<TextureHandle>* null_tex = asset_manager_texture_item_get(vk_ctx->null_texture_handle);
    if (item)
    {
        os_mutex_scope_w(asset_manager->texture_mutex)
        {
            if (asset_manager->shutting_down == false)
            {
                descriptor_set_clear_bindless_texture(item->item.descriptor_set_idx, null_tex);
            }
            descriptor_index_free(&asset_manager->descriptor_index_allocator, item->item.descriptor_set_idx);
            texture_destroy(&item->item);
            asset_manager_item_free(item, &asset_manager->texture_list, &asset_manager->texture_free_list);
        }
    }
}

static void
asset_manager_handle_free(render::Handle handle)
{
    if (render::is_handle_zero(handle))
        return;

    switch (handle.type)
    {
        case render::HandleType::Buffer:
        {
            asset_manager_buffer_free(handle);
        }
        break;
        case render::HandleType::Texture:
        {
            asset_manager_texture_free(handle);
        }
        break;
        default: InvalidPath; break;
    }
    Debug_Asset_Push_Deletion(handle);
}

//~mgj: Buffer Allocation Functions (VMA)

static void
buffer_destroy(BufferAllocation* buffer_allocation)
{
    AssetManager* asset_manager = asset_manager_get();
    if (buffer_allocation->buffer != VK_NULL_HANDLE)
    {
        MEMORY_LOG("VMA Buffer Destroyed: %p (size: %llu bytes)", buffer_allocation->buffer, buffer_allocation->size);
        vmaDestroyBuffer(asset_manager->allocator, buffer_allocation->buffer, buffer_allocation->allocation);
        *buffer_allocation = {};
    }
}

static BufferAllocation
_staging_buffer_create(VkDeviceSize size)
{
    VmaAllocationCreateInfo vma_staging_info = {0};
    vma_staging_info.usage = VMA_MEMORY_USAGE_AUTO;
    vma_staging_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    vma_staging_info.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    BufferAllocation staging_buffer = _buffer_allocation_create(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &vma_staging_info, nullptr);
    return staging_buffer;
}

static BufferAllocation
_staging_buffer_mapped_create(VkDeviceSize size)
{
    VmaAllocationCreateInfo staging_alloc_create_info = {};
    staging_alloc_create_info.usage = VMA_MEMORY_USAGE_AUTO;
    staging_alloc_create_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    BufferAllocation allocation = _buffer_allocation_create(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &staging_alloc_create_info, nullptr);

    return allocation;
}

static BufferAllocation
_buffer_allocation_create(VkDeviceSize size, VkBufferUsageFlags buffer_usage, VmaAllocationCreateInfo* vma_info, VmaAllocationInfo* alloc_info)
{
    AssetManager* asset_manager = asset_manager_get();
    BufferAllocation buffer = {};

    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = buffer_usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VK_CHECK_RESULT(vmaCreateBuffer(asset_manager->allocator, &bufferInfo, vma_info, &buffer.buffer, &buffer.allocation, alloc_info));
    buffer.size = (U32)size;

    MEMORY_LOG("VMA Buffer Allocated: %p (size: %llu bytes, usage: 0x%x)", buffer.buffer, buffer.size, buffer_usage);

    return buffer;
}

static render::Handle
buffer_alloc_create_or_resize(U32 total_buffer_byte_count, render::Handle handle, VkBufferUsageFlags usage)
{
    // buffer alloc get
    render::BufferType buffer_type = buffer_type_from_usage_flags(usage);
    render::Handle final_handle = handle;
    if (render::is_handle_zero(final_handle))
    {
        final_handle = render::Handle::buffer_handle_create(buffer_type);
    }
    render::AssetItem<BufferHandle>* asset_item_buffer = asset_manager_buffer_item_get(final_handle);
    Assert(asset_item_buffer);

    if (total_buffer_byte_count > asset_item_buffer->item.buffer_alloc.size)
    {
        deletion_queue_push(final_handle);
        final_handle = render::Handle::buffer_handle_create(buffer_type);
        asset_item_buffer = asset_manager_buffer_item_get(final_handle);
        Assert(asset_item_buffer);

        VmaAllocationCreateInfo vma_info = {0};
        vma_info.usage = VMA_MEMORY_USAGE_AUTO;
        vma_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        vma_info.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;

        asset_item_buffer->item.buffer_alloc = _buffer_allocation_create(total_buffer_byte_count, usage, &vma_info, nullptr);
        asset_item_buffer->item.item_byte_size = 1;
        asset_item_buffer->item.elem_count = total_buffer_byte_count;
    }
    return final_handle;
}

static void
buffer_readback_create(VkDeviceSize size, VkBufferUsageFlags buffer_usage, BufferReadback* out_buffer_readback)
{
    VmaAllocationCreateInfo alloc_create_info = {};
    alloc_create_info.usage = VMA_MEMORY_USAGE_AUTO;
    alloc_create_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo alloc_info = {};
    BufferAllocation allocation = _buffer_allocation_create(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | buffer_usage, &alloc_create_info, &alloc_info);
    out_buffer_readback->buffer_alloc = allocation;
    out_buffer_readback->mapped_ptr = alloc_info.pMappedData;
}

static void
buffer_readback_destroy(BufferReadback* out_buffer_readback)
{
    buffer_destroy(&out_buffer_readback->buffer_alloc);
    out_buffer_readback->mapped_ptr = 0;
}

g_internal void*
asset_manager_allocation_cpu_pointer_get(void* allocation)
{
    AssetManager* asset_manager = asset_manager_get();
    VmaAllocationInfo alloc_info;
    vmaGetAllocationInfo(asset_manager->allocator, (VmaAllocation)allocation, &alloc_info);
    return alloc_info.pMappedData;
}
//~mgj: Image Allocation Functions (VMA)

//~mgj: Image Allocation Functions (VMA)
static ImageAllocation
image_allocation_create(U32 width, U32 height, VkSampleCountFlagBits numSamples, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, U32 mipmap_level, VmaAllocationCreateInfo vma_info,
                        const char* name, VkImageType image_type)
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
    VK_CHECK_RESULT(vmaCreateImage(asset_manager->allocator, &image_create_info, &vma_info, &image_alloc.image, &image_alloc.allocation, &alloc_info))
    vmaSetAllocationName(asset_manager->allocator, image_alloc.allocation, name);
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

// debug helpers
lib_internal void
asset_manager_debug_name_set(void* allocation, String8 name)
{
    AssetManager* asset_manager = asset_manager_get();
    if (name.size > 0)
    {
        vmaSetAllocationName(asset_manager->allocator, (VmaAllocation)allocation, (char*)name.str);
    }
}

} // namespace vulkan

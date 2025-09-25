static R_Handle
R_Tex2dAlloc(R_ResourceKind kind, Vec2S32 size, R_Tex2DFormat format, void* data)
{
    (void)kind; // ignoreing kind at the moment
    U32 mip_levels = 1;
    wrapper::VulkanContext* vk_ctx = wrapper::VulkanCtxGet();

    // ~mgj Create Vulkan Image
    VkFormat vk_format = wrapper::R_VkFormatFromTex2DFormat(
        format); // is format function correct? use UNORM instead of SRGB
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
    R_Handle texture_handle = wrapper::VK_DescriptorSetCreate(
        vk_ctx->arena, vk_ctx->device, vk_ctx->descriptor_pool, vk_ctx->font_descriptor_set_layout,
        image_view_resource.image_view, vk_sampler);

    // ~mgj: save texture as asset
    R_AssetId asset_id = {.id = texture_handle.u64[0]};
    R_AssetItem<wrapper::Texture>* asset_item = wrapper::AssetManagerTextureItemGet(asset_id);
    asset_item->id = asset_id;
    asset_item->item.handle = texture_handle;
    asset_item->item.image_resource = image_resource;
    asset_item->item.sampler = vk_sampler;

    return texture_handle;
}

static void
R_FillTex2dRegion(R_Handle handle, Rng2S32 subrect, void* data)
{
}

static void
R_Tex2dRelease(R_Handle texture)
{
}

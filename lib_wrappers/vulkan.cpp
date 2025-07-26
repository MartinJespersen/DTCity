

namespace wrapper
{

static void
RoadRenderPass(wrapper::VulkanContext* vk_ctx, wrapper::Road* w_road, city::Road* road,
               U32 image_index)
{
    VkExtent2D swap_chain_extent = vk_ctx->swapchain_extent;
    VkCommandBuffer command_buffer = vk_ctx->command_buffers.data[vk_ctx->current_frame];

    // Color attachment (assuming you want to render to the same targets)
    VkRenderingAttachmentInfo color_attachment{};
    color_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    color_attachment.imageView = vk_ctx->color_image_resource.image_view_resource.image_view;
    color_attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; // Load existing content
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}}; // Clear to black

    // Depth attachment
    VkRenderingAttachmentInfo depth_attachment{};
    depth_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depth_attachment.imageView = vk_ctx->depth_image_resource.image_view_resource.image_view;
    depth_attachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; // Load existing depth
    depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depth_attachment.clearValue.depthStencil = {1.0f, 0}; // Clear to far plane

    // Set up MSAA resolve if needed
    if (vk_ctx->msaa_samples != VK_SAMPLE_COUNT_1_BIT)
    {
        color_attachment.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
        color_attachment.resolveImageView =
            vk_ctx->swapchain_image_resources.data[image_index].image_view_resource.image_view;
        color_attachment.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }

    // Rendering info
    VkRenderingInfo rendering_info{};
    rendering_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering_info.renderArea.offset = {0, 0};
    rendering_info.renderArea.extent = vk_ctx->swapchain_extent;
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

    RoadPushConstants road_push_constant = {.model = road->model_matrix,
                                            .road_width = road->road_width,
                                            .road_height = road->road_height};
    vkCmdPushConstants(command_buffer, w_road->pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                       sizeof(RoadPushConstants), &road_push_constant);
    VkDescriptorSet descriptor_sets[] = {vk_ctx->camera_descriptor_sets[vk_ctx->current_frame]};
    vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            w_road->pipeline_layout, 0, ArrayCount(descriptor_sets),
                            descriptor_sets, 0, NULL);
    vkCmdBindVertexBuffers(command_buffer, 0, 1, vertex_buffers, offsets);

    vkCmdDraw(command_buffer, w_road->vertex_buffer.size, 1, 0, 0);

    vkCmdEndRendering(command_buffer);
}
static void
RoadInit(wrapper::VulkanContext* vk_ctx, city::City* city, String8 cwd)
{
    internal::RoadPipelineCreate(city, cwd);
}

static void
RoadUpdate(city::Road* road, Road* w_road, wrapper::VulkanContext* vk_ctx, U32 image_index)
{
    // TODO: this should be mapped to GPU memory (DEVICE_LOCAL)
    internal::VkBufferFromBufferMapping<city::RoadVertex>(
        vk_ctx, &w_road->vertex_buffer, road->vertex_buffer, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    RoadRenderPass(vk_ctx, w_road, road, image_index);
}

static void
RoadCleanup(city::City* city, wrapper::VulkanContext* vk_ctx)
{
    Road* road = city->w_road;
    internal::BufferContextDestroy(vk_ctx->allocator, &road->vertex_buffer);
    vkDestroyPipelineLayout(vk_ctx->device, road->pipeline_layout, nullptr);
    vkDestroyPipeline(vk_ctx->device, road->pipeline, nullptr);
}

static void
VK_ImageLayoutTransition(VkCommandBuffer command_buffer, VkImage image, VkFormat format,
                         VkImageLayout oldLayout, VkImageLayout newLayout, U32 mipmap_level)
{
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = mipmap_level;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags source_stage;
    VkPipelineStageFlags destination_stage;
    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
    {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        source_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destination_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
             newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        source_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destination_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    else
    {
        exitWithError("unsupported layout transition!");
    }

    vkCmdPipelineBarrier(command_buffer, source_stage, destination_stage, 0, 0, nullptr, 0, nullptr,
                         1, &barrier);
}

static void
VK_GenerateMipmaps(VkCommandBuffer command_buffer, VkImage image, int32_t tex_width,
                   int32_t text_height, uint32_t mip_levels)
{
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.image = image;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.subresourceRange.levelCount = 1;

    S32 mip_width = tex_width;
    S32 mip_height = text_height;

    for (U32 i = 1; i < mip_levels; i++)
    {
        barrier.subresourceRange.baseMipLevel = i - 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                             &barrier);

        VkImageBlit blit{};
        blit.srcOffsets[0] = {0, 0, 0};
        blit.srcOffsets[1] = {mip_width, mip_height, 1};
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.mipLevel = i - 1;
        blit.srcSubresource.baseArrayLayer = 0;
        blit.srcSubresource.layerCount = 1;
        blit.dstOffsets[0] = {0, 0, 0};
        blit.dstOffsets[1] = {mip_width > 1 ? mip_width / 2 : 1,
                              mip_height > 1 ? mip_height / 2 : 1, 1};
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.mipLevel = i;
        blit.dstSubresource.baseArrayLayer = 0;
        blit.dstSubresource.layerCount = 1;

        vkCmdBlitImage(command_buffer, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                             &barrier);

        if (mip_width > 1)
            mip_width /= 2;
        if (mip_height > 1)
            mip_height /= 2;
    }

    barrier.subresourceRange.baseMipLevel = mip_levels - 1;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                         &barrier);
}

// Samplers helpers
static void
VK_SamplerCreate(VkSampler* sampler, VkDevice device, VkFilter filter,
                 VkSamplerMipmapMode mipmap_mode, U32 mip_level_count, F32 max_anisotrophy)
{
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = filter;
    samplerInfo.minFilter = filter;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    samplerInfo.addressModeV = samplerInfo.addressModeU;
    samplerInfo.addressModeW = samplerInfo.addressModeU;
    samplerInfo.compareOp = VK_COMPARE_OP_NEVER;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.mipmapMode = mipmap_mode;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.anisotropyEnable = VK_TRUE;
    samplerInfo.maxAnisotropy = max_anisotrophy; // Use max anisotropy supported

    if (vkCreateSampler(device, &samplerInfo, nullptr, sampler) != VK_SUCCESS)
    {
        exitWithError("failed to create texture sampler!");
    }
}

static VkResult
CreateDebugUtilsMessengerEXT(VkInstance instance,
                             const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
                             const VkAllocationCallbacks* pAllocator,
                             VkDebugUtilsMessengerEXT* pDebugMessenger)
{
    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
        instance, "vkCreateDebugUtilsMessengerEXT");
    if (func != nullptr)
    {
        return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
    }
    else
    {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
}

static void
DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger,
                              const VkAllocationCallbacks* pAllocator)
{
    auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
        instance, "vkDestroyDebugUtilsMessengerEXT");
    if (func != nullptr)
    {
        func(instance, debugMessenger, pAllocator);
    }
}

static VkCommandBuffer
VK_BeginSingleTimeCommands(VulkanContext* vk_ctx)
{
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = vk_ctx->command_pool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(vk_ctx->device, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    return commandBuffer;
}

static void
VK_EndSingleTimeCommands(VulkanContext* vk_ctx, VkCommandBuffer command_buffer)
{
    vkEndCommandBuffer(command_buffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &command_buffer;

    vkQueueSubmit(vk_ctx->graphics_queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(vk_ctx->graphics_queue);

    vkFreeCommandBuffers(vk_ctx->device, vk_ctx->command_pool, 1, &command_buffer);
}

static uint32_t
VK_FindMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter,
                  VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
    {
        if ((typeFilter & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
        {
            return i;
        }
    }

    exitWithError("failed to find suitable memory type!");
    return 0;
}

// queue family

VkFormat
VK_SupportedFormat(VkPhysicalDevice physical_device, const VkFormat candidates[3],
                   VkImageTiling tiling, VkFormatFeatureFlags features)
{
    for (U32 i = 0; i < ArrayCount(candidates); i++)
    {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(physical_device, candidates[i], &props);

        if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features)
        {
            return candidates[i];
        }
        else if (tiling == VK_IMAGE_TILING_OPTIMAL &&
                 (props.optimalTilingFeatures & features) == features)
        {
            return candidates[i];
        }
    }

    exitWithError("failed to find supported format!");
    return VK_FORMAT_UNDEFINED;
}

static void
VK_DepthResourcesCreate(VulkanContext* vk_ctx)
{
    Temp scratch = ScratchBegin(0, 0);
    VkFormat depth_formats[3] = {VK_FORMAT_D16_UNORM, VK_FORMAT_D32_SFLOAT,
                                 VK_FORMAT_D24_UNORM_S8_UINT};

    VkFormat depth_format =
        VK_SupportedFormat(vk_ctx->physical_device, depth_formats, VK_IMAGE_TILING_OPTIMAL,
                           VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
    vk_ctx->depth_attachment_format = depth_format;

    B32 has_stencil_component =
        depth_format == VK_FORMAT_D32_SFLOAT_S8_UINT || depth_format == VK_FORMAT_D24_UNORM_S8_UINT;

    internal::ImageAllocation image_alloc = internal::ImageAllocationCreate(
        vk_ctx->allocator, vk_ctx->swapchain_extent.width, vk_ctx->swapchain_extent.height,
        vk_ctx->msaa_samples, depth_format, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, 1, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, 0);

    internal::ImageViewResource image_view_resource = internal::ImageViewResourceCreate(
        vk_ctx->device, image_alloc.image, depth_format, VK_IMAGE_ASPECT_DEPTH_BIT, 1);

    vk_ctx->depth_image_resource = {.image_alloc = image_alloc,
                                    .image_view_resource = image_view_resource};
    ScratchEnd(scratch);
}

static void
VK_CommandPoolCreate(VulkanContext* vk_ctx)
{
    internal::QueueFamilyIndices queueFamilyIndices = vk_ctx->queue_family_indices;

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamilyIndex;

    if (vkCreateCommandPool(vk_ctx->device, &poolInfo, nullptr, &vk_ctx->command_pool) !=
        VK_SUCCESS)
    {
        exitWithError("failed to create command pool!");
    }
}

static void
VK_SurfaceCreate(VulkanContext* vk_ctx, IO* io)
{
    int supported = glfwVulkanSupported();
    B32 enabled = supported == GLFW_TRUE;

    VkResult result =
        glfwCreateWindowSurface(vk_ctx->instance, io->window, nullptr, &vk_ctx->surface);
    if (result != VK_SUCCESS)
    {
        exitWithError("failed to create window surface!");
    }
}

static void
VK_CreateInstance(VulkanContext* vk_ctx)
{
    Temp scratch = ScratchBegin(0, 0);
    if (vk_ctx->enable_validation_layers && !VK_CheckValidationLayerSupport(vk_ctx))
    {
        exitWithError("validation layers requested, but not available!");
    }

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Hello Triangle";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    Buffer<String8> extensions = VK_RequiredExtensionsGet(vk_ctx);

    createInfo.enabledExtensionCount = (U32)extensions.size;
    createInfo.ppEnabledExtensionNames = CStrArrFromStr8Buffer(scratch.arena, extensions);

    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    if (vk_ctx->enable_validation_layers)
    {
        createInfo.enabledLayerCount = (U32)vk_ctx->validation_layers.size;
        createInfo.ppEnabledLayerNames =
            CStrArrFromStr8Buffer(scratch.arena, vk_ctx->validation_layers);

        VK_PopulateDebugMessengerCreateInfo(debugCreateInfo);
        createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT*)&debugCreateInfo;
    }
    else
    {
        createInfo.enabledLayerCount = 0;
        createInfo.pNext = nullptr;
    }

    if (vkCreateInstance(&createInfo, nullptr, &vk_ctx->instance) != VK_SUCCESS)
    {
        exitWithError("failed to create instance!");
    }

    ScratchEnd(scratch);
}

static void
VK_LogicalDeviceCreate(Arena* arena, VulkanContext* vk_ctx)
{
    internal::QueueFamilyIndices queueFamilyIndicies = vk_ctx->queue_family_indices;

    U32 uniqueQueueFamiliesCount = 1;
    if (queueFamilyIndicies.graphicsFamilyIndex != queueFamilyIndicies.presentFamilyIndex)
    {
        uniqueQueueFamiliesCount++;
    }

    U32 uniqueQueueFamilies[] = {queueFamilyIndicies.graphicsFamilyIndex,
                                 queueFamilyIndicies.presentFamilyIndex};

    VkDeviceQueueCreateInfo* queueCreateInfos =
        PushArray(arena, VkDeviceQueueCreateInfo, uniqueQueueFamiliesCount);
    float queuePriority = 1.0f;
    for (U32 i = 0; i < uniqueQueueFamiliesCount; i++)
    {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = uniqueQueueFamilies[i];
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos[i] = queueCreateInfo;
    }

    VkPhysicalDeviceFeatures deviceFeatures{};
    deviceFeatures.samplerAnisotropy = VK_TRUE;
    deviceFeatures.sampleRateShading = VK_TRUE;
    deviceFeatures.tessellationShader = VK_TRUE;
    deviceFeatures.geometryShader = VK_TRUE;
    deviceFeatures.fillModeNonSolid = VK_TRUE;

    VkPhysicalDeviceDynamicRenderingFeatures dynamic_rendering_features{};
    dynamic_rendering_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES;
    dynamic_rendering_features.dynamicRendering = VK_TRUE;

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pNext = &dynamic_rendering_features;
    createInfo.pQueueCreateInfos = queueCreateInfos;

    createInfo.queueCreateInfoCount = uniqueQueueFamiliesCount;

    createInfo.pEnabledFeatures = &deviceFeatures;

    createInfo.enabledExtensionCount = (U32)vk_ctx->device_extensions.size;
    createInfo.ppEnabledExtensionNames =
        CStrArrFromStr8Buffer(vk_ctx->arena, vk_ctx->device_extensions);

    // NOTE: This if statement is no longer necessary on newer versions
    if (vk_ctx->enable_validation_layers)
    {
        createInfo.enabledLayerCount = (U32)vk_ctx->validation_layers.size;
        createInfo.ppEnabledLayerNames =
            CStrArrFromStr8Buffer(vk_ctx->arena, vk_ctx->validation_layers);
    }
    else
    {
        createInfo.enabledLayerCount = 0;
    }

    if (vkCreateDevice(vk_ctx->physical_device, &createInfo, nullptr, &vk_ctx->device) !=
        VK_SUCCESS)
    {
        exitWithError("failed to create logical device!");
    }

    vkGetDeviceQueue(vk_ctx->device, queueFamilyIndicies.graphicsFamilyIndex, 0,
                     &vk_ctx->graphics_queue);
    vkGetDeviceQueue(vk_ctx->device, queueFamilyIndicies.presentFamilyIndex, 0,
                     &vk_ctx->present_queue);
}

static void
VK_PhysicalDevicePick(VulkanContext* vk_ctx)
{
    Temp scratch = ScratchBegin(0, 0);

    vk_ctx->physical_device = VK_NULL_HANDLE;

    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(vk_ctx->instance, &deviceCount, nullptr);

    if (deviceCount == 0)
    {
        exitWithError("failed to find GPUs with Vulkan support!");
    }

    VkPhysicalDevice* devices = PushArray(scratch.arena, VkPhysicalDevice, deviceCount);
    vkEnumeratePhysicalDevices(vk_ctx->instance, &deviceCount, devices);

    for (U32 i = 0; i < deviceCount; i++)
    {
        internal::QueueFamilyIndexBits familyIndexBits =
            internal::VK_QueueFamiliesFind(vk_ctx, devices[i]);
        if (VK_IsDeviceSuitable(vk_ctx, devices[i], familyIndexBits))
        {
            VkPhysicalDeviceProperties properties{};
            vkGetPhysicalDeviceProperties(devices[i], &properties);

            vk_ctx->physical_device = devices[i];
            vk_ctx->physical_device_properties = properties;
            vk_ctx->msaa_samples = VK_MaxUsableSampleCountGet(devices[i]);
            vk_ctx->queue_family_indices = VK_QueueFamilyIndicesFromBitFields(familyIndexBits);
            break;
        }
    }

    if (vk_ctx->physical_device == VK_NULL_HANDLE)
    {
        exitWithError("failed to find a suitable GPU!");
    }

    ScratchEnd(scratch);
}

static bool
VK_CheckValidationLayerSupport(VulkanContext* vk_ctx)
{
    Temp scratch = ScratchBegin(0, 0);
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

    VkLayerProperties* availableLayers = PushArray(scratch.arena, VkLayerProperties, layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers);

    bool layerFound = false;
    for (U32 i = 0; i < vk_ctx->validation_layers.size; i++)
    {
        layerFound = false;
        for (U32 j = 0; j < layerCount; j++)
        {
            if (strcmp((char*)vk_ctx->validation_layers.data[i].str,
                       availableLayers[j].layerName) == 0)
            {
                layerFound = true;
                break;
            }
        }

        if (!layerFound)
        {
            break;
        }
    }

    ScratchEnd(scratch);
    return layerFound;
}

static Buffer<String8>
VK_RequiredExtensionsGet(VulkanContext* vk_ctx)
{
    U32 glfwExtensionCount = 0;
    const char** glfwExtensions;
    glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    U32 extensionCount = glfwExtensionCount;
    if (vk_ctx->enable_validation_layers)
    {
        extensionCount++;
    }

    Buffer<String8> extensions = BufferAlloc<String8>(vk_ctx->arena, extensionCount);

    for (U32 i = 0; i < glfwExtensionCount; i++)
    {
        extensions.data[i] = push_str8f(vk_ctx->arena, glfwExtensions[i]);
    }

    if (vk_ctx->enable_validation_layers)
    {
        extensions.data[glfwExtensionCount] =
            push_str8f(vk_ctx->arena, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    return extensions;
}

static VKAPI_ATTR VkBool32 VKAPI_CALL
VK_DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                 VkDebugUtilsMessageTypeFlagsEXT messageType,
                 const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData)
{
    (void)pUserData;
    (void)messageType;
    (void)messageSeverity;
    printf("validation layer: %s\n", pCallbackData->pMessage);

    return VK_FALSE;
}

static void
VK_DebugMessengerSetup(VulkanContext* vk_ctx)
{
    if (!vk_ctx->enable_validation_layers)
        return;
    VkDebugUtilsMessengerCreateInfoEXT createInfo;
    VK_PopulateDebugMessengerCreateInfo(createInfo);
    if (CreateDebugUtilsMessengerEXT(vk_ctx->instance, &createInfo, nullptr,
                                     &vk_ctx->debug_messenger) != VK_SUCCESS)
    {
        exitWithError("failed to set up debug messenger!");
    }
}

static void
VK_PopulateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo)
{
    createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = VK_DebugCallback;
}

static bool
VK_CheckDeviceExtensionSupport(VulkanContext* vk_ctx, VkPhysicalDevice device)
{
    Temp scratch = ScratchBegin(0, 0);
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

    VkExtensionProperties* availableExtensions =
        PushArray(scratch.arena, VkExtensionProperties, extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions);
    const U64 numberOfRequiredExtenstions = vk_ctx->device_extensions.size;
    U64 numberOfRequiredExtenstionsLeft = numberOfRequiredExtenstions;
    for (U32 i = 0; i < extensionCount; i++)
    {
        for (U32 j = 0; j < numberOfRequiredExtenstions; j++)
        {
            if (CStrEqual((char*)vk_ctx->device_extensions.data[j].str,
                          availableExtensions[i].extensionName))
            {
                numberOfRequiredExtenstionsLeft--;
                break;
            }
        }
    }

    ScratchEnd(scratch);
    return numberOfRequiredExtenstionsLeft == 0;
}

static VkSurfaceFormatKHR
VK_ChooseSwapSurfaceFormat(Buffer<VkSurfaceFormatKHR> availableFormats)
{
    for (U32 i = 0; i < availableFormats.size; i++)
    {
        if (availableFormats.data[i].format == VK_FORMAT_B8G8R8A8_SRGB &&
            availableFormats.data[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            return availableFormats.data[i];
        }
    }
    return availableFormats.data[0];
}

static VkPresentModeKHR
VK_ChooseSwapPresentMode(Buffer<VkPresentModeKHR> availablePresentModes)
{
    for (U32 i = 0; i < availablePresentModes.size; i++)
    {
        if (availablePresentModes.data[i] == VK_PRESENT_MODE_MAILBOX_KHR)
        {
            return availablePresentModes.data[i];
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

static VkExtent2D
VK_ChooseSwapExtent(IO* io_ctx, VulkanContext* vk_ctx, const VkSurfaceCapabilitiesKHR& capabilities)
{
    if (capabilities.currentExtent.width != UINT32_MAX)
    {
        return capabilities.currentExtent;
    }
    else
    {
        int width, height;
        glfwGetFramebufferSize(io_ctx->window, &width, &height);

        VkExtent2D actualExtent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};

        actualExtent.width = Clamp(actualExtent.width, capabilities.minImageExtent.width,
                                   capabilities.maxImageExtent.width);
        actualExtent.height = Clamp(actualExtent.height, capabilities.minImageExtent.height,
                                    capabilities.maxImageExtent.height);

        return actualExtent;
    }
}

static VkSampleCountFlagBits
VK_MaxUsableSampleCountGet(VkPhysicalDevice device)
{
    VkPhysicalDeviceProperties physicalDeviceProperties;
    vkGetPhysicalDeviceProperties(device, &physicalDeviceProperties);

    VkSampleCountFlags counts = physicalDeviceProperties.limits.framebufferColorSampleCounts &
                                physicalDeviceProperties.limits.framebufferDepthSampleCounts;
    if (counts & VK_SAMPLE_COUNT_64_BIT)
    {
        return VK_SAMPLE_COUNT_64_BIT;
    }
    if (counts & VK_SAMPLE_COUNT_32_BIT)
    {
        return VK_SAMPLE_COUNT_32_BIT;
    }
    if (counts & VK_SAMPLE_COUNT_16_BIT)
    {
        return VK_SAMPLE_COUNT_16_BIT;
    }
    if (counts & VK_SAMPLE_COUNT_8_BIT)
    {
        return VK_SAMPLE_COUNT_8_BIT;
    }
    if (counts & VK_SAMPLE_COUNT_4_BIT)
    {
        return VK_SAMPLE_COUNT_4_BIT;
    }
    if (counts & VK_SAMPLE_COUNT_2_BIT)
    {
        return VK_SAMPLE_COUNT_2_BIT;
    }

    return VK_SAMPLE_COUNT_1_BIT;
}

static void
VK_RecreateSwapChain(IO* io_ctx, VulkanContext* vk_ctx)
{
    Temp scratch = ScratchBegin(0, 0);
    int width = 0, height = 0;
    glfwGetFramebufferSize(io_ctx->window, &width, &height);
    while (width == 0 || height == 0)
    {
        glfwGetFramebufferSize(io_ctx->window, &width, &height);
        glfwWaitEvents();
    }
    vkDeviceWaitIdle(vk_ctx->device);

    VK_SwapChainCleanup(vk_ctx);

    internal::SwapChainInfo swapchain_info =
        internal::VK_SwapChainCreate(scratch.arena, vk_ctx, io_ctx);
    U32 swapchain_image_count = internal::VK_SwapChainImageCountGet(vk_ctx);
    internal::SwapChainImageResourceCreate(vk_ctx, swapchain_info, swapchain_image_count);

    internal::VK_ColorResourcesCreate(vk_ctx);

    VK_DepthResourcesCreate(vk_ctx);
    ScratchEnd(scratch);
}

static void
VK_Cleanup(VulkanContext* vk_ctx)
{
    vkDeviceWaitIdle(vk_ctx->device);

    if (vk_ctx->enable_validation_layers)
    {
        DestroyDebugUtilsMessengerEXT(vk_ctx->instance, vk_ctx->debug_messenger, nullptr);
    }
    // indice and vertex buffers
    BufferContextDestroy(vk_ctx->allocator, &vk_ctx->vk_indice_context);
    BufferContextDestroy(vk_ctx->allocator, &vk_ctx->vk_vertex_context);

    internal::CameraCleanup(vk_ctx);

    VK_SwapChainCleanup(vk_ctx);

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
    vkDestroyDevice(vk_ctx->device, nullptr);
    vkDestroyInstance(vk_ctx->instance, nullptr);
}

static void
VK_ColorResourcesCleanup(VulkanContext* vk_ctx)
{
    internal::ImageResourceDestroy(vk_ctx->allocator, vk_ctx->color_image_resource);
}

static void
VK_DepthResourcesCleanup(VulkanContext* vk_ctx)
{
    internal::ImageResourceDestroy(vk_ctx->allocator, vk_ctx->depth_image_resource);
}

static void
VK_SwapChainCleanup(VulkanContext* vk_ctx)
{
    VK_ColorResourcesCleanup(vk_ctx);
    VK_DepthResourcesCleanup(vk_ctx);

    for (size_t i = 0; i < vk_ctx->swapchain_image_resources.size; i++)
    {
        internal::ImageViewResourceDestroy(
            vk_ctx->swapchain_image_resources.data[i].image_view_resource);
    }

    vkDestroySwapchainKHR(vk_ctx->device, vk_ctx->swapchain, nullptr);
}

static void
VK_SyncObjectsCreate(VulkanContext* vk_ctx)
{
    vk_ctx->image_available_semaphores =
        BufferAlloc<VkSemaphore>(vk_ctx->arena, vk_ctx->MAX_FRAMES_IN_FLIGHT);
    vk_ctx->render_finished_semaphores =
        BufferAlloc<VkSemaphore>(vk_ctx->arena, vk_ctx->MAX_FRAMES_IN_FLIGHT);
    vk_ctx->in_flight_fences = BufferAlloc<VkFence>(vk_ctx->arena, vk_ctx->MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (U32 i = 0; i < (U32)vk_ctx->MAX_FRAMES_IN_FLIGHT; i++)
    {
        if (vkCreateSemaphore(vk_ctx->device, &semaphoreInfo, nullptr,
                              &vk_ctx->image_available_semaphores.data[i]) != VK_SUCCESS ||
            vkCreateSemaphore(vk_ctx->device, &semaphoreInfo, nullptr,
                              &vk_ctx->render_finished_semaphores.data[i]) != VK_SUCCESS ||
            vkCreateFence(vk_ctx->device, &fenceInfo, nullptr, &vk_ctx->in_flight_fences.data[i]) !=
                VK_SUCCESS)
        {
            exitWithError("failed to create synchronization objects for a frame!");
        }
    }
}

static void
VK_CommandBuffersCreate(VulkanContext* vk_ctx)
{
    vk_ctx->command_buffers =
        BufferAlloc<VkCommandBuffer>(vk_ctx->arena, vk_ctx->MAX_FRAMES_IN_FLIGHT);
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = vk_ctx->command_pool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = (uint32_t)vk_ctx->command_buffers.size;

    if (vkAllocateCommandBuffers(vk_ctx->device, &allocInfo, vk_ctx->command_buffers.data) !=
        VK_SUCCESS)
    {
        exitWithError("failed to allocate command buffers!");
    }
}

static VulkanContext*
VK_VulkanInit(Arena* arena, IO* io_ctx)
{
    VulkanContext* vk_ctx = PushStruct(arena, VulkanContext);

    Temp scratch = ScratchBegin(0, 0);
    vk_ctx->arena = ArenaAlloc();

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
    internal::SwapChainInfo swapchain_info =
        internal::VK_SwapChainCreate(scratch.arena, vk_ctx, io_ctx);
    U32 swapchain_image_count = internal::VK_SwapChainImageCountGet(vk_ctx);

    internal::SwapChainImageResourceCreate(vk_ctx, swapchain_info, swapchain_image_count);
    vk_ctx->color_attachment_format = vk_ctx->swapchain_image_format;

    VK_CommandPoolCreate(vk_ctx);

    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.physicalDevice = vk_ctx->physical_device;
    allocatorInfo.device = vk_ctx->device;
    allocatorInfo.instance = vk_ctx->instance;
    vmaCreateAllocator(&allocatorInfo, &vk_ctx->allocator);

    internal::VK_ColorResourcesCreate(vk_ctx);

    VK_DepthResourcesCreate(vk_ctx);

    VK_CommandBuffersCreate(vk_ctx);

    VK_SyncObjectsCreate(vk_ctx);

    internal::DescriptorPoolCreate(vk_ctx);
    internal::CameraUniformBufferCreate(vk_ctx);
    internal::CameraDescriptorSetLayoutCreate(vk_ctx);
    internal::CameraDescriptorSetCreate(vk_ctx);
    ScratchEnd(scratch);

    return vk_ctx;
}

namespace internal
{
static void
RoadPipelineCreate(city::City* city, String8 cwd)
{
    ScratchScope scratch = ScratchScope(0, 0);
    wrapper::Road* w_road = city->w_road;

    VulkanContext* vk_ctx = GlobalContextGet()->vk_ctx;

    String8 vert_path = CreatePathFromStrings(
        scratch.arena,
        Str8BufferFromCString(scratch.arena, {(char*)cwd.str, "shaders", "road", "road_vert.spv"}));
    String8 frag_path = CreatePathFromStrings(
        scratch.arena,
        Str8BufferFromCString(scratch.arena, {(char*)cwd.str, "shaders", "road", "road_frag.spv"}));

    internal::ShaderModuleInfo vert_shader_stage_info = internal::ShaderStageFromSpirv(
        scratch.arena, vk_ctx->device, VK_SHADER_STAGE_VERTEX_BIT, vert_path);
    internal::ShaderModuleInfo frag_shader_stage_info = internal::ShaderStageFromSpirv(
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
        internal::RoadAttributeDescriptionGet(scratch.arena);
    VkVertexInputBindingDescription input_desc = internal::RoadBindingDescriptionGet();
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

    // Add push constant range for geometry shader
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(RoadPushConstants);

    VkDescriptorSetLayout descriptor_set_layouts[1] = {vk_ctx->camera_descriptor_set_layout};
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
                               &w_road->pipeline_layout) != VK_SUCCESS)
    {
        exitWithError("failed to create pipeline layout!");
    }

    VkPipelineRenderingCreateInfo pipeline_rendering_info{};
    pipeline_rendering_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    pipeline_rendering_info.colorAttachmentCount = 1;
    pipeline_rendering_info.pColorAttachmentFormats = &vk_ctx->color_attachment_format;
    pipeline_rendering_info.depthAttachmentFormat = vk_ctx->depth_attachment_format;

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

    pipelineInfo.layout = w_road->pipeline_layout;
    pipelineInfo.renderPass = VK_NULL_HANDLE;

    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE; // Optional
    pipelineInfo.basePipelineIndex = -1;              // Optional

    if (vkCreateGraphicsPipelines(vk_ctx->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr,
                                  &w_road->pipeline) != VK_SUCCESS)
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

static ShaderModuleInfo
ShaderStageFromSpirv(Arena* arena, VkDevice device, VkShaderStageFlagBits flag, String8 path)
{
    internal::ShaderModuleInfo shader_module_info = {};
    shader_module_info.device = device;
    Buffer<U8> shader_buffer = IO_ReadFile(arena, path);

    shader_module_info.info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_module_info.info.stage = flag;
    shader_module_info.info.module = ShaderModuleCreate(device, shader_buffer);
    shader_module_info.info.pName = "main";

    return shader_module_info;
}

static VkShaderModule
ShaderModuleCreate(VkDevice device, Buffer<U8> buffer)
{
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = buffer.size;
    createInfo.pCode = reinterpret_cast<const U32*>(buffer.data);

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS)
    {
        exitWithError("failed to create shader module!");
    }

    return shaderModule;
}
static void
BufferDestroy(VmaAllocator allocator, BufferAllocation buffer_allocation)
{
    vmaDestroyBuffer(allocator, buffer_allocation.buffer, buffer_allocation.allocation);
}

static void
BufferMappedDestroy(VmaAllocator allocator, internal::BufferAllocationMapped mapped_buffer)
{
    BufferDestroy(allocator, mapped_buffer.buffer_alloc);
    BufferDestroy(allocator, mapped_buffer.staging_buffer_alloc);
    ArenaRelease(mapped_buffer.arena);
}

static void
BufferContextDestroy(VmaAllocator allocator, BufferContext* buffer_context)
{
    vmaDestroyBuffer(allocator, buffer_context->buffer_alloc.buffer,
                     buffer_context->buffer_alloc.allocation);
}

template <typename T>
static void
VkBufferFromBuffers(VulkanContext* vk_ctx, BufferContext* vk_buffer_ctx, Buffer<Buffer<T>> buffers,
                    VkBufferUsageFlags usage)
{
    // calculate number of vertices
    U32 total_buffer_size = 0;
    for (U32 buf_i = 0; buf_i < buffers.size; buf_i++)
    {
        Buffer<T> buffer = buffers.data[buf_i];
        total_buffer_size += buffer.size;
    }

    if (total_buffer_size)
    {
        VkDeviceSize buffer_byte_size = sizeof(T) * total_buffer_size;
        if (total_buffer_size > vk_buffer_ctx->capacity)
        {
            BufferDestroy(vk_ctx->allocator, vk_buffer_ctx->buffer_alloc);

            vk_buffer_ctx->buffer_alloc = BufferAllocationCreate(
                vk_ctx->allocator, buffer_byte_size, usage,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

            vk_buffer_ctx->capacity = total_buffer_size;
        }

        // TODO: Consider using vkFlushMappedMemoryRanges and vkInvalidateMappedMemoryRanges instead
        // of VK_MEMORY_PROPERTY_HOST_COHERENT_BIT for performance.
        void* data;
        vmaMapMemory(vk_ctx->allocator, vk_buffer_ctx->buffer_alloc.allocation, &data);

        U32 data_offset = 0;
        for (U32 buf_i = 0; buf_i < buffers.size; buf_i++)
        {
            Buffer<T> buffer = buffers.data[buf_i];
            MemoryCopy((T*)data + data_offset, buffer.data, buffer.size * sizeof(T));
            data_offset += buffer.size;
        }
        vmaUnmapMemory(vk_ctx->allocator, vk_buffer_ctx->buffer_alloc.allocation);

        vk_buffer_ctx->size = total_buffer_size;
    }
}

template <typename T>
static void
VkBufferFromBufferMapping(VulkanContext* vk_ctx, BufferContext* vk_buffer_ctx, Buffer<T> buffer,
                          VkBufferUsageFlags usage)
{
    // calculate number of vertices
    U32 total_buffer_size = buffer.size;

    if (total_buffer_size)
    {
        VkDeviceSize buffer_byte_size = sizeof(T) * total_buffer_size;
        if (total_buffer_size > vk_buffer_ctx->capacity)
        {
            BufferContextDestroy(vk_ctx->allocator, vk_buffer_ctx);

            vk_buffer_ctx->buffer_alloc = BufferAllocationCreate(
                vk_ctx->allocator, buffer_byte_size, usage, VMA_MEMORY_USAGE_CPU_ONLY,
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                    VMA_ALLOCATION_CREATE_MAPPED_BIT);

            vk_buffer_ctx->capacity = total_buffer_size;
        }
        vk_buffer_ctx->size = total_buffer_size;

        void* data;
        vmaMapMemory(vk_ctx->allocator, vk_buffer_ctx->buffer_alloc.allocation, &data);

        MemoryCopy((T*)data, buffer.data, total_buffer_size * sizeof(T));
        vmaUnmapMemory(vk_ctx->allocator, vk_buffer_ctx->buffer_alloc.allocation);
    }
}

static QueueFamilyIndices
VK_QueueFamilyIndicesFromBitFields(QueueFamilyIndexBits queueFamilyBits)
{
    if (!VK_QueueFamilyIsComplete(queueFamilyBits))
    {
        exitWithError(
            "Queue family is not complete either graphics or present queue is not supported");
    }

    QueueFamilyIndices indices = {0};
    indices.graphicsFamilyIndex = (U32)LSBIndex((S32)queueFamilyBits.graphicsFamilyIndexBits);
    indices.presentFamilyIndex = (U32)LSBIndex((S32)queueFamilyBits.presentFamilyIndexBits);

    return indices;
}

static QueueFamilyIndexBits
VK_QueueFamiliesFind(VulkanContext* vk_ctx, VkPhysicalDevice device)
{
    Temp scratch = ScratchBegin(0, 0);
    QueueFamilyIndexBits indices = {0};
    U32 queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

    VkQueueFamilyProperties* queueFamilies =
        PushArray(scratch.arena, VkQueueFamilyProperties, queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies);
    for (U32 i = 0; i < queueFamilyCount; i++)
    {
        if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
        {
            indices.graphicsFamilyIndexBits |= (1 << i);
        }

        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, vk_ctx->surface, &presentSupport);
        if (presentSupport)
        {
            indices.presentFamilyIndexBits |= (1 << i);
        }
    }

    U32 sameFamily = indices.graphicsFamilyIndexBits & indices.presentFamilyIndexBits;
    if (sameFamily)
    {
        sameFamily &= ((~sameFamily) + 1);
        indices.graphicsFamilyIndexBits = sameFamily;
        indices.presentFamilyIndexBits = sameFamily;
    }

    indices.graphicsFamilyIndexBits &= (~indices.graphicsFamilyIndexBits) + 1;
    indices.presentFamilyIndexBits &= (~indices.presentFamilyIndexBits) + 1;

    ScratchEnd(scratch);
    return indices;
}

static bool
VK_IsDeviceSuitable(VulkanContext* vk_ctx, VkPhysicalDevice device, QueueFamilyIndexBits indexBits)
{
    // NOTE: This is where you would implement your own checks to see if the
    // device is suitable for your needs

    bool extensionsSupported = VK_CheckDeviceExtensionSupport(vk_ctx, device);

    bool swapChainAdequate = false;
    if (extensionsSupported)
    {
        SwapChainSupportDetails swapChainDetails =
            VK_QuerySwapChainSupport(vk_ctx->arena, vk_ctx, device);
        swapChainAdequate = swapChainDetails.formats.size && swapChainDetails.presentModes.size;
    }

    VkPhysicalDeviceFeatures supportedFeatures;
    vkGetPhysicalDeviceFeatures(device, &supportedFeatures);

    return VK_QueueFamilyIsComplete(indexBits) && extensionsSupported && swapChainAdequate &&
           supportedFeatures.samplerAnisotropy;
}

static bool
VK_QueueFamilyIsComplete(QueueFamilyIndexBits queueFamily)
{
    if ((!queueFamily.graphicsFamilyIndexBits) || (!queueFamily.presentFamilyIndexBits))
    {
        return false;
    }
    return true;
}

static SwapChainSupportDetails
VK_QuerySwapChainSupport(Arena* arena, VulkanContext* vk_ctx, VkPhysicalDevice device)
{
    SwapChainSupportDetails details;

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, vk_ctx->surface, &details.capabilities);

    U32 formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, vk_ctx->surface, &formatCount, nullptr);

    if (formatCount != 0)
    {
        details.formats = BufferAlloc<VkSurfaceFormatKHR>(arena, formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, vk_ctx->surface, &formatCount,
                                             details.formats.data);
    }

    U32 presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, vk_ctx->surface, &presentModeCount, nullptr);

    if (presentModeCount != 0)
    {
        details.presentModes = BufferAlloc<VkPresentModeKHR>(arena, presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, vk_ctx->surface, &presentModeCount,
                                                  details.presentModes.data);
    }

    return details;
}

// ~mgj: Descriptor pool creation
static void
DescriptorPoolCreate(VulkanContext* vk_ctx)
{
    U32 max_frames_in_flight = vk_ctx->MAX_FRAMES_IN_FLIGHT;
    VkDescriptorPoolSize pool_sizes[] = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
         max_frames_in_flight * 2}, // 2 for both camera buffer and terrain buffer
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, max_frames_in_flight},
    };
    U32 pool_size_count = ArrayCount(pool_sizes);

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = pool_size_count;
    poolInfo.pPoolSizes = pool_sizes;

    poolInfo.maxSets = max_frames_in_flight * 2;

    if (vkCreateDescriptorPool(vk_ctx->device, &poolInfo, nullptr, &vk_ctx->descriptor_pool) !=
        VK_SUCCESS)
    {
        exitWithError("failed to create descriptor pool!");
    }
}
// ~mgj: Camera functions
static void
FrustumPlanesCalculate(Frustum* out_frustum, const glm::mat4 matrix)
{
    out_frustum->planes[LEFT].x = matrix[0].w + matrix[0].x;
    out_frustum->planes[LEFT].y = matrix[1].w + matrix[1].x;
    out_frustum->planes[LEFT].z = matrix[2].w + matrix[2].x;
    out_frustum->planes[LEFT].w = matrix[3].w + matrix[3].x;

    out_frustum->planes[RIGHT].x = matrix[0].w - matrix[0].x;
    out_frustum->planes[RIGHT].y = matrix[1].w - matrix[1].x;
    out_frustum->planes[RIGHT].z = matrix[2].w - matrix[2].x;
    out_frustum->planes[RIGHT].w = matrix[3].w - matrix[3].x;

    out_frustum->planes[TOP].x = matrix[0].w - matrix[0].y;
    out_frustum->planes[TOP].y = matrix[1].w - matrix[1].y;
    out_frustum->planes[TOP].z = matrix[2].w - matrix[2].y;
    out_frustum->planes[TOP].w = matrix[3].w - matrix[3].y;

    out_frustum->planes[BOTTOM].x = matrix[0].w + matrix[0].y;
    out_frustum->planes[BOTTOM].y = matrix[1].w + matrix[1].y;
    out_frustum->planes[BOTTOM].z = matrix[2].w + matrix[2].y;
    out_frustum->planes[BOTTOM].w = matrix[3].w + matrix[3].y;

    out_frustum->planes[BACK].x = matrix[0].w + matrix[0].z;
    out_frustum->planes[BACK].y = matrix[1].w + matrix[1].z;
    out_frustum->planes[BACK].z = matrix[2].w + matrix[2].z;
    out_frustum->planes[BACK].w = matrix[3].w + matrix[3].z;

    out_frustum->planes[FRONT].x = matrix[0].w - matrix[0].z;
    out_frustum->planes[FRONT].y = matrix[1].w - matrix[1].z;
    out_frustum->planes[FRONT].z = matrix[2].w - matrix[2].z;
    out_frustum->planes[FRONT].w = matrix[3].w - matrix[3].z;

    for (size_t i = 0; i < ArrayCount(out_frustum->planes); i++)
    {
        float length = sqrtf(out_frustum->planes[i].x * out_frustum->planes[i].x +
                             out_frustum->planes[i].y * out_frustum->planes[i].y +
                             out_frustum->planes[i].z * out_frustum->planes[i].z);
        out_frustum->planes[i] /= length;
    }
}

static void
CameraUniformBufferCreate(VulkanContext* vk_ctx)
{
    VkDeviceSize camera_buffer_size = sizeof(CameraUniformBuffer);
    VkBufferUsageFlags buffer_usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

    for (U32 i = 0; i < vk_ctx->MAX_FRAMES_IN_FLIGHT; i++)
    {
        vk_ctx->camera_buffer_alloc_mapped[i] = BufferMappedCreate(
            vk_ctx->command_buffers.data[i], vk_ctx->allocator, camera_buffer_size, buffer_usage);
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
        exitWithError("failed to allocate descriptor sets!");
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

static void
ImageViewResourceDestroy(ImageViewResource image_view_resource)
{
    vkDestroyImageView(image_view_resource.device, image_view_resource.image_view, 0);
}

static ImageResource
ImageResourceCreate(ImageViewResource image_view_resource, ImageAllocation image_alloc,
                    VkImageView image_view)
{
    return {.image_alloc = image_alloc, .image_view_resource = image_view_resource};
}

static void
ImageAllocationDestroy(VmaAllocator allocator, ImageAllocation image_alloc)
{
    vmaDestroyImage(allocator, image_alloc.image, image_alloc.allocation);
}

static void
ImageResourceDestroy(VmaAllocator allocator, internal::ImageResource image)
{
    ImageViewResourceDestroy(image.image_view_resource);
    ImageAllocationDestroy(allocator, image.image_alloc);
}

static internal::BufferAllocation
BufferAllocationCreate(VmaAllocator allocator, VkDeviceSize size, VkBufferUsageFlags buffer_usage,
                       VmaMemoryUsage vma_usage, VmaAllocationCreateFlags vma_flags)
{
    internal::BufferAllocation buffer = {};
    buffer.size = size;

    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = buffer_usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo vmaallocInfo = {};
    vmaallocInfo.usage = vma_usage;

    if (vmaCreateBuffer(allocator, &bufferInfo, &vmaallocInfo, &buffer.buffer, &buffer.allocation,
                        nullptr) != VK_SUCCESS)
    {
        exitWithError("Failed to create buffer!");
    }

    return buffer;
}

// inspiration:
// https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/usage_patterns.html
static internal::BufferAllocationMapped
BufferMappedCreate(VkCommandBuffer cmd_buffer, VmaAllocator allocator, VkDeviceSize size,
                   VkBufferUsageFlags buffer_usage)
{
    Arena* arena = ArenaAlloc();

    internal::BufferAllocation buffer = BufferAllocationCreate(
        allocator, size, buffer_usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_AUTO,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
            VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT |
            VMA_ALLOCATION_CREATE_MAPPED_BIT);

    VkMemoryPropertyFlags mem_prop_flags;
    vmaGetAllocationMemoryProperties(allocator, buffer.allocation, &mem_prop_flags);

    VmaAllocationInfo alloc_info;
    vmaGetAllocationInfo(allocator, buffer.allocation, &alloc_info);

    internal::BufferAllocationMapped mapped_buffer = {
        .buffer_alloc = buffer, .mem_prop_flags = mem_prop_flags, .arena = arena};

    if (!alloc_info.pMappedData)
    {
        mapped_buffer.mapped_ptr = (void*)PushArray(mapped_buffer.arena, U8, size);
        mapped_buffer.staging_buffer_alloc = BufferAllocationCreate(
            allocator, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                VMA_ALLOCATION_CREATE_MAPPED_BIT);
    }

    return mapped_buffer;
}

static void
BufferMappedUpdate(VkCommandBuffer cmd_buffer, VmaAllocator allocator,
                   internal::BufferAllocationMapped mapped_buffer)
{
    if (mapped_buffer.mem_prop_flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
    {
        if ((mapped_buffer.mem_prop_flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0)
        {
            vmaFlushAllocation(allocator, mapped_buffer.buffer_alloc.allocation, 0,
                               mapped_buffer.buffer_alloc.size);
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
        if (vmaCopyMemoryToAllocation(allocator, mapped_buffer.mapped_ptr,
                                      mapped_buffer.staging_buffer_alloc.allocation, 0,
                                      mapped_buffer.buffer_alloc.size))
        {
            exitWithError("BufferMappedUpdate: Could not copy data to staging buffer");
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
            mapped_buffer.buffer_alloc.size,
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

static ImageViewResource
ImageViewResourceCreate(VkDevice device, VkImage image, VkFormat format,
                        VkImageAspectFlags aspect_mask, U32 mipmap_level)
{
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = aspect_mask;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = mipmap_level;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VkImageView view;
    if (vkCreateImageView(device, &viewInfo, nullptr, &view) != VK_SUCCESS)
    {
        exitWithError("failed to create texture image view!");
    }

    return {.image_view = view, .device = device};
}

static ImageAllocation
ImageAllocationCreate(VmaAllocator allocator, U32 width, U32 height,
                      VkSampleCountFlagBits numSamples, VkFormat format, VkImageTiling tiling,
                      VkImageUsageFlags usage, U32 mipmap_level, VmaMemoryUsage memory_usage,
                      VmaAllocationCreateFlags memory_properties)
{
    ImageAllocation image_alloc = {0};

    VkImageCreateInfo image_create_info{};
    image_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_create_info.imageType = VK_IMAGE_TYPE_2D;
    image_create_info.extent.width = width;
    image_create_info.extent.height = height;
    image_create_info.extent.depth = 1;
    image_create_info.mipLevels = mipmap_level;
    image_create_info.arrayLayers = 1;
    image_create_info.format = format;
    image_create_info.tiling = tiling;
    image_create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_create_info.usage = usage;
    image_create_info.samples = numSamples;
    image_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo alloc_create_info = {};
    alloc_create_info.usage = memory_usage;
    alloc_create_info.flags = memory_properties;

    if (vmaCreateImage(allocator, &image_create_info, &alloc_create_info, &image_alloc.image,
                       &image_alloc.allocation, 0))
    {
        exitWithError("ImageCreate: Could not create image");
    };

    return image_alloc;
}

static void
VK_ColorResourcesCreate(VulkanContext* vk_ctx)
{
    VkFormat colorFormat = vk_ctx->color_attachment_format;

    internal::ImageAllocation image_alloc = ImageAllocationCreate(
        vk_ctx->allocator, vk_ctx->swapchain_extent.width, vk_ctx->swapchain_extent.height,
        vk_ctx->msaa_samples, colorFormat, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, 1, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, 0);

    ImageViewResource color_image_view = ImageViewResourceCreate(
        vk_ctx->device, image_alloc.image, colorFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1);

    vk_ctx->color_image_resource = {.image_alloc = image_alloc,
                                    .image_view_resource = color_image_view};
}
static void
SwapChainImageResourceCreate(VulkanContext* vk_ctx, internal::SwapChainInfo swapchain_info,
                             U32 image_count)
{
    ScratchScope scratch = ScratchScope(0, 0);

    vk_ctx->swapchain_image_resources =
        BufferAlloc<internal::ImageSwapchainResource>(vk_ctx->arena, image_count);

    VkImage* images = PushArray(scratch.arena, VkImage, image_count);
    if (vkGetSwapchainImagesKHR(vk_ctx->device, vk_ctx->swapchain, &image_count, images) !=
        VK_SUCCESS)
    {
        exitWithError("failed to get swapchain images!");
    }

    vk_ctx->swapchain_image_format = swapchain_info.surfaceFormat.format;
    vk_ctx->swapchain_extent = swapchain_info.extent;

    for (uint32_t i = 0; i < image_count; i++)
    {
        internal::ImageViewResource image_view_resource = internal::ImageViewResourceCreate(
            vk_ctx->device, images[i], vk_ctx->swapchain_image_format, VK_IMAGE_ASPECT_COLOR_BIT,
            1);

        vk_ctx->swapchain_image_resources.data[i] = {.image = images[i],
                                                     .image_view_resource = image_view_resource};
    }
}

static U32
VK_SwapChainImageCountGet(VulkanContext* vk_ctx)
{
    U32 imageCount = {0};
    if (vkGetSwapchainImagesKHR(vk_ctx->device, vk_ctx->swapchain, &imageCount, nullptr) !=
        VK_SUCCESS)
    {
        exitWithError("failed to get swapchain image count!");
    }
    return imageCount;
}

static SwapChainInfo
VK_SwapChainCreate(Arena* arena, VulkanContext* vk_ctx, IO* io_ctx)
{
    SwapChainInfo swapChainInfo = {0};

    swapChainInfo.swapChainSupport =
        internal::VK_QuerySwapChainSupport(arena, vk_ctx, vk_ctx->physical_device);

    swapChainInfo.surfaceFormat =
        VK_ChooseSwapSurfaceFormat(swapChainInfo.swapChainSupport.formats);
    swapChainInfo.presentMode =
        VK_ChooseSwapPresentMode(swapChainInfo.swapChainSupport.presentModes);
    swapChainInfo.extent =
        VK_ChooseSwapExtent(io_ctx, vk_ctx, swapChainInfo.swapChainSupport.capabilities);

    U32 imageCount = swapChainInfo.swapChainSupport.capabilities.minImageCount + 1;

    if (swapChainInfo.swapChainSupport.capabilities.maxImageCount > 0 &&
        imageCount > swapChainInfo.swapChainSupport.capabilities.maxImageCount)
    {
        imageCount = swapChainInfo.swapChainSupport.capabilities.maxImageCount;
    }

    internal::QueueFamilyIndices queueFamilyIndices = vk_ctx->queue_family_indices;
    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = vk_ctx->surface;

    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = swapChainInfo.surfaceFormat.format;
    createInfo.imageColorSpace = swapChainInfo.surfaceFormat.colorSpace;
    createInfo.imageExtent = swapChainInfo.extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    if (queueFamilyIndices.graphicsFamilyIndex != queueFamilyIndices.presentFamilyIndex)
    {
        U32 queueFamilyIndicesSame[] = {vk_ctx->queue_family_indices.graphicsFamilyIndex,
                                        vk_ctx->queue_family_indices.presentFamilyIndex};
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndicesSame;
    }
    else
    {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        createInfo.queueFamilyIndexCount = 0;     // Optional
        createInfo.pQueueFamilyIndices = nullptr; // Optional
    }

    createInfo.preTransform = swapChainInfo.swapChainSupport.capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = swapChainInfo.presentMode;
    createInfo.clipped = VK_TRUE;
    // TODO: It is possible to specify the old swap chain to be replaced by a new one
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(vk_ctx->device, &createInfo, nullptr, &vk_ctx->swapchain) !=
        VK_SUCCESS)
    {
        exitWithError("failed to create swap chain!");
    }

    return swapChainInfo;
}

} // namespace internal

} // namespace wrapper

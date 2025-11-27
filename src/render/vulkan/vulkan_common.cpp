

// TODO: check for blitting format beforehand

static VkResult
VK_CreateDebugUtilsMessengerEXT(VkInstance instance,
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
VK_DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger,
                                 const VkAllocationCallbacks* pAllocator)
{
    auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
        instance, "vkDestroyDebugUtilsMessengerEXT");
    if (func != nullptr)
    {
        func(instance, debugMessenger, pAllocator);
    }
}

// queue family
static VkFormat
VK_SupportedFormat(VkPhysicalDevice physical_device, VkFormat* candidates, U32 candidate_count,
                   VkImageTiling tiling, VkFormatFeatureFlags features)
{
    VkFormat format = VK_FORMAT_UNDEFINED;
    for (U32 i = 0; i < candidate_count; i++)
    {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(physical_device, candidates[i], &props);

        if (tiling == VK_IMAGE_TILING_LINEAR &&
                ((props.linearTilingFeatures & features) == features) ||
            ((props.optimalTilingFeatures & features) == features))
        {
            return candidates[i];
        }
    }

    exit_with_error("failed to find supported format!");
    return format;
}

static void
VK_DepthResourcesCreate(VK_Context* vk_ctx, vk_SwapchainResources* swapchain_resources)
{
    VkFormat depth_formats[3] = {
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D24_UNORM_S8_UINT,
        VK_FORMAT_D16_UNORM,
    };

    VkFormat depth_format =
        VK_SupportedFormat(vk_ctx->physical_device, depth_formats, ArrayCount(depth_formats),
                           VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
    swapchain_resources->depth_format = depth_format;

    VmaAllocationCreateInfo vma_info = {
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
    };

    VK_ImageAllocation image_alloc = VK_ImageAllocationCreate(
        vk_ctx->allocator, swapchain_resources->swapchain_extent.width,
        swapchain_resources->swapchain_extent.height, vk_ctx->msaa_samples, depth_format,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, 1, vma_info);

    VK_ImageViewResource image_view_resource = VK_ImageViewResourceCreate(
        vk_ctx->device, image_alloc.image, depth_format, VK_IMAGE_ASPECT_DEPTH_BIT, 1);

    swapchain_resources->depth_image_resource = {.image_alloc = image_alloc,
                                                 .image_view_resource = image_view_resource};
}

static void
ObjectIdImageResourceCreate(vk_SwapchainResources* swapchain_resources, U32 image_count)
{
    VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL;
    VK_Context* vk_ctx = VK_CtxGet();
    VkFormat attachment_formats[] = {vk_ctx->object_id_format};

    VkFormat attachment_format = VK_SupportedFormat(vk_ctx->physical_device, attachment_formats,
                                                    ArrayCount(attachment_formats), tiling,
                                                    VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT);

    VmaAllocationCreateInfo vma_info = {.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
                                        .usage = VMA_MEMORY_USAGE_AUTO,
                                        .priority = 1.0f};

    Buffer<VK_ImageResource> object_id_image_resources =
        BufferAlloc<VK_ImageResource>(swapchain_resources->arena, image_count);
    Buffer<VK_ImageResource> object_id_image_resolve_resources =
        BufferAlloc<VK_ImageResource>(swapchain_resources->arena, image_count);

    U32 buffer_size = 0;
    for (U32 i = 0; i < image_count; i++)
    {
        // multisampled image
        VK_ImageResource* object_id_image_resource = &object_id_image_resources.data[i];
        VK_ImageAllocation* image_alloc = &object_id_image_resource->image_alloc;
        VK_ImageViewResource* image_view_resource = &object_id_image_resource->image_view_resource;

        *image_alloc = VK_ImageAllocationCreate(
            vk_ctx->allocator, swapchain_resources->swapchain_extent.width,
            swapchain_resources->swapchain_extent.height, vk_ctx->msaa_samples, attachment_format,
            tiling,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            1, vma_info);

        *image_view_resource = VK_ImageViewResourceCreate(
            vk_ctx->device, image_alloc->image, attachment_format, VK_IMAGE_ASPECT_COLOR_BIT, 1);

        // resolve image
        VK_ImageResource* object_id_image_resolve_resource =
            &object_id_image_resolve_resources.data[i];
        VK_ImageAllocation* image_resolve_alloc = &object_id_image_resolve_resource->image_alloc;
        VK_ImageViewResource* image_resolve_view_resource =
            &object_id_image_resolve_resource->image_view_resource;

        *image_resolve_alloc = VK_ImageAllocationCreate(
            vk_ctx->allocator, swapchain_resources->swapchain_extent.width,
            swapchain_resources->swapchain_extent.height, VK_SAMPLE_COUNT_1_BIT, attachment_format,
            tiling,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            1, vma_info);

        *image_resolve_view_resource =
            VK_ImageViewResourceCreate(vk_ctx->device, image_resolve_alloc->image,
                                       attachment_format, VK_IMAGE_ASPECT_COLOR_BIT, 1);

        buffer_size = image_resolve_alloc->size;
    }

    swapchain_resources->object_id_image_format = attachment_format;
    swapchain_resources->object_id_image_resources = object_id_image_resources;
    swapchain_resources->object_id_image_resolve_resources = object_id_image_resolve_resources;

    VK_BufferReadbackCreate(vk_ctx->allocator, buffer_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                            &swapchain_resources->object_id_buffer_readback);
}

static VkCommandPool
VK_CommandPoolCreate(VkDevice device, VkCommandPoolCreateInfo* poolInfo)
{
    VkCommandPool cmd_pool;
    if (vkCreateCommandPool(device, poolInfo, nullptr, &cmd_pool) != VK_SUCCESS)
    {
        exit_with_error("failed to create command pool!");
    }
    return cmd_pool;
}

static void
VK_SurfaceCreate(VK_Context* vk_ctx, io_IO* io)
{
    int supported = glfwVulkanSupported();
    if (supported != GLFW_TRUE)
    {
        exit_with_error("Vulkan loader or ICD loading failed!");
    }

    VkResult result =
        glfwCreateWindowSurface(vk_ctx->instance, io->window, nullptr, &vk_ctx->surface);
    if (result != VK_SUCCESS)
    {
        exit_with_error("failed to create window surface!");
    }
}

static void
VK_CreateInstance(VK_Context* vk_ctx)
{
    Temp scratch = ScratchBegin(0, 0);
    if (vk_ctx->enable_validation_layers && !VK_CheckValidationLayerSupport(vk_ctx))
    {
        exit_with_error("validation layers requested, but not available!");
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

    VK_CHECK_RESULT(vkCreateInstance(&createInfo, nullptr, &vk_ctx->instance));

    ScratchEnd(scratch);
}

static void
VK_LogicalDeviceCreate(Arena* arena, VK_Context* vk_ctx)
{
    VK_QueueFamilyIndices queueFamilyIndicies = vk_ctx->queue_family_indices;

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

    VkPhysicalDeviceColorWriteEnableFeaturesEXT colorWriteEnableFeatures = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COLOR_WRITE_ENABLE_FEATURES_EXT,
        .colorWriteEnable = VK_TRUE,
    };

    // setup linked list of device features
    VkPhysicalDeviceSynchronization2Features sync2_features{};
    sync2_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES;
    sync2_features.synchronization2 = VK_TRUE;
    sync2_features.pNext = &colorWriteEnableFeatures;

    VkPhysicalDeviceDynamicRenderingFeatures dynamic_rendering_features{};
    dynamic_rendering_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES;
    dynamic_rendering_features.dynamicRendering = VK_TRUE;
    dynamic_rendering_features.pNext = &sync2_features;

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
        exit_with_error("failed to create logical device!");
    }

    vkGetDeviceQueue(vk_ctx->device, queueFamilyIndicies.graphicsFamilyIndex, 0,
                     &vk_ctx->graphics_queue);
    vkGetDeviceQueue(vk_ctx->device, queueFamilyIndicies.presentFamilyIndex, 0,
                     &vk_ctx->present_queue);
    vk_cmd_set_color_write_enable_ext = (PFN_vkCmdSetColorWriteEnableEXT)vkGetDeviceProcAddr(
        vk_ctx->device, "vkCmdSetColorWriteEnableEXT");
    if (!vk_cmd_set_color_write_enable_ext)
    {
        exit_with_error("Could not load vkCmdSetColorWriteEnableEXT");
    }
}

static void
VK_PhysicalDevicePick(VK_Context* vk_ctx)
{
    Temp scratch = ScratchBegin(0, 0);

    vk_ctx->physical_device = VK_NULL_HANDLE;

    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(vk_ctx->instance, &deviceCount, nullptr);

    if (deviceCount == 0)
    {
        exit_with_error("failed to find GPUs with Vulkan support!");
    }

    VkPhysicalDevice* devices = PushArray(scratch.arena, VkPhysicalDevice, deviceCount);
    vkEnumeratePhysicalDevices(vk_ctx->instance, &deviceCount, devices);

    for (U32 i = 0; i < deviceCount; i++)
    {
        VK_QueueFamilyIndexBits familyIndexBits = VK_QueueFamiliesFind(vk_ctx, devices[i]);
        if (VK_IsDeviceSuitable(vk_ctx, devices[i], familyIndexBits))
        {
            VkPhysicalDeviceProperties properties{};
            vkGetPhysicalDeviceProperties(devices[i], &properties);

            DEBUG_LOG("Name of device: %s\nDevice Type: %d\n", properties.deviceName,
                      properties.deviceType);
            vk_ctx->physical_device = devices[i];
            vk_ctx->physical_device_properties = properties;
            vk_ctx->msaa_samples = VK_MaxUsableSampleCountGet(devices[i]);
            vk_ctx->queue_family_indices = VK_QueueFamilyIndicesFromBitFields(familyIndexBits);
            break;
        }
    }

    if (vk_ctx->physical_device == VK_NULL_HANDLE)
    {
        exit_with_error("failed to find a suitable GPU!");
    }

    ScratchEnd(scratch);
}

static bool
VK_CheckValidationLayerSupport(VK_Context* vk_ctx)
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
            printf("Could not find validation layer %s\n", vk_ctx->validation_layers.data[i].str);
            break;
        }
    }

    ScratchEnd(scratch);
    return layerFound;
}

static Buffer<String8>
VK_RequiredExtensionsGet(VK_Context* vk_ctx)
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
    (void)pCallbackData;
    (void)pUserData;
    (void)messageType;
    (void)messageSeverity;
    DEBUG_LOG("validation layer: %s\n", pCallbackData->pMessage);

    return VK_FALSE;
}

static void
VK_DebugMessengerSetup(VK_Context* vk_ctx)
{
    if (!vk_ctx->enable_validation_layers)
        return;
    VkDebugUtilsMessengerCreateInfoEXT createInfo;
    VK_PopulateDebugMessengerCreateInfo(createInfo);
    if (VK_CreateDebugUtilsMessengerEXT(vk_ctx->instance, &createInfo, nullptr,
                                        &vk_ctx->debug_messenger) != VK_SUCCESS)
    {
        exit_with_error("failed to set up debug messenger!");
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
VK_CheckDeviceExtensionSupport(VK_Context* vk_ctx, VkPhysicalDevice device)
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

static void
VK_ColorResourcesCleanup(VmaAllocator allocator, VK_ImageResource color_image_resource)
{
    VK_ImageResourceDestroy(allocator, color_image_resource);
}

static void
VK_DepthResourcesCleanup(VmaAllocator allocator, VK_ImageResource depth_image_resource)
{
    VK_ImageResourceDestroy(allocator, depth_image_resource);
}

static void
ObjectIdResourcesCleanup()
{
    VK_Context* vk_ctx = VK_CtxGet();
    vk_SwapchainResources* swapchain_resources = vk_ctx->swapchain_resources;
    for (U32 i = 0; i < swapchain_resources->object_id_image_resources.size; i++)
    {
        VK_ImageResourceDestroy(vk_ctx->allocator,
                                swapchain_resources->object_id_image_resources.data[i]);
        VK_ImageResourceDestroy(vk_ctx->allocator,
                                swapchain_resources->object_id_image_resolve_resources.data[i]);
    }

    VK_BufferReadbackDestroy(vk_ctx->allocator, &swapchain_resources->object_id_buffer_readback);
}

static void
vk_sync_objects_create(VK_Context* vk_ctx)
{
    vk_ctx->in_flight_fences = BufferAlloc<VkFence>(vk_ctx->arena, VK_MAX_FRAMES_IN_FLIGHT);

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (U32 i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; i++)
    {
        if ((vkCreateFence(vk_ctx->device, &fenceInfo, nullptr,
                           &vk_ctx->in_flight_fences.data[i]) != VK_SUCCESS))
        {
            exit_with_error("failed to fences synchronization objects for a frame!");
        }
    }
}

static void
VK_SyncObjectsDestroy(VK_Context* vk_ctx)
{
    for (size_t i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; i++)
    {
        vkDestroyFence(vk_ctx->device, vk_ctx->in_flight_fences.data[i], nullptr);
    }
}

static void
VK_CommandBuffersCreate(VK_Context* vk_ctx)
{
    vk_ctx->command_buffers = BufferAlloc<VkCommandBuffer>(vk_ctx->arena, VK_MAX_FRAMES_IN_FLIGHT);
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = vk_ctx->command_pool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = (uint32_t)vk_ctx->command_buffers.size;

    if (vkAllocateCommandBuffers(vk_ctx->device, &allocInfo, vk_ctx->command_buffers.data) !=
        VK_SUCCESS)
    {
        exit_with_error("failed to allocate command buffers!");
    }
}

static VK_ShaderModuleInfo
VK_ShaderStageFromSpirv(Arena* arena, VkDevice device, VkShaderStageFlagBits flag, String8 path)
{
    VK_ShaderModuleInfo shader_module_info = {};
    shader_module_info.device = device;
    Buffer<U8> shader_buffer = io_file_read(arena, path);

    shader_module_info.info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_module_info.info.stage = flag;
    shader_module_info.info.module = VK_ShaderModuleCreate(device, shader_buffer);
    shader_module_info.info.pName = "main";

    return shader_module_info;
}

static VkShaderModule
VK_ShaderModuleCreate(VkDevice device, Buffer<U8> buffer)
{
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = buffer.size;
    createInfo.pCode = reinterpret_cast<const U32*>(buffer.data);

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS)
    {
        exit_with_error("failed to create shader module!");
    }

    return shaderModule;
}
static void
VK_BufferDestroy(VmaAllocator allocator, VK_BufferAllocation* buffer_allocation)
{
    vmaDestroyBuffer(allocator, buffer_allocation->buffer, buffer_allocation->allocation);
    buffer_allocation->buffer = VK_NULL_HANDLE;
}

static void
VK_BufferMappedDestroy(VmaAllocator allocator, VK_BufferAllocationMapped* mapped_buffer)
{
    VK_BufferDestroy(allocator, &mapped_buffer->buffer_alloc);
    VK_BufferDestroy(allocator, &mapped_buffer->staging_buffer_alloc);
    ArenaRelease(mapped_buffer->arena);
}

static VK_BufferAllocation
VK_StagingBufferCreate(VmaAllocator allocator, VkDeviceSize size)
{
    VmaAllocationCreateInfo vma_staging_info = {0};
    vma_staging_info.usage = VMA_MEMORY_USAGE_AUTO;
    vma_staging_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    vma_staging_info.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    VK_BufferAllocation staging_buffer = VK_BufferAllocationCreate(
        allocator, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, vma_staging_info);
    return staging_buffer;
}

static void
VK_BufferAllocCreateOrResize(VmaAllocator allocator, U32 total_buffer_byte_count,
                             VK_BufferAllocation* buffer_alloc, VkBufferUsageFlags usage)
{
    if (total_buffer_byte_count > buffer_alloc->size)
    {
        VK_BufferDestroy(allocator, buffer_alloc);

        VmaAllocationCreateInfo vma_info = {0};
        vma_info.usage = VMA_MEMORY_USAGE_AUTO;
        vma_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                         VMA_ALLOCATION_CREATE_MAPPED_BIT;
        vma_info.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;

        *buffer_alloc =
            VK_BufferAllocationCreate(allocator, total_buffer_byte_count, usage, vma_info);
    }
}

static VK_QueueFamilyIndices
VK_QueueFamilyIndicesFromBitFields(VK_QueueFamilyIndexBits queueFamilyBits)
{
    if (!VK_QueueFamilyIsComplete(queueFamilyBits))
    {
        exit_with_error("Queue family is not complete either graphics or present "
                        "queue is not supported");
    }

    VK_QueueFamilyIndices indices = {0};
    indices.graphicsFamilyIndex = (U32)LSBIndex((S32)queueFamilyBits.graphicsFamilyIndexBits);
    indices.presentFamilyIndex = (U32)LSBIndex((S32)queueFamilyBits.presentFamilyIndexBits);

    return indices;
}

static VK_QueueFamilyIndexBits
VK_QueueFamiliesFind(VK_Context* vk_ctx, VkPhysicalDevice device)
{
    Temp scratch = ScratchBegin(0, 0);
    VK_QueueFamilyIndexBits indices = {0};
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
VK_IsDeviceSuitable(VK_Context* vk_ctx, VkPhysicalDevice device, VK_QueueFamilyIndexBits indexBits)
{
    // NOTE: This is where you would implement your own checks to see if the
    // device is suitable for your needs

    bool extensionsSupported = VK_CheckDeviceExtensionSupport(vk_ctx, device);

    bool swapChainAdequate = false;
    if (extensionsSupported)
    {
        VK_SwapChainSupportDetails swapChainDetails =
            VK_QuerySwapChainSupport(vk_ctx->arena, device, vk_ctx->surface);
        swapChainAdequate = swapChainDetails.formats.size && swapChainDetails.presentModes.size;
    }

    VkPhysicalDeviceFeatures supportedFeatures;
    vkGetPhysicalDeviceFeatures(device, &supportedFeatures);

    return VK_QueueFamilyIsComplete(indexBits) && extensionsSupported && swapChainAdequate &&
           supportedFeatures.samplerAnisotropy;
}

static bool
VK_QueueFamilyIsComplete(VK_QueueFamilyIndexBits queueFamily)
{
    if ((!queueFamily.graphicsFamilyIndexBits) || (!queueFamily.presentFamilyIndexBits))
    {
        return false;
    }
    return true;
}

static VK_SwapChainSupportDetails
VK_QuerySwapChainSupport(Arena* arena, VkPhysicalDevice device, VkSurfaceKHR surface)
{
    VK_SwapChainSupportDetails details;

    VK_CHECK_RESULT(
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities));

    U32 formatCount;
    VK_CHECK_RESULT(vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr));

    if (formatCount != 0)
    {
        details.formats = BufferAlloc<VkSurfaceFormatKHR>(arena, formatCount);
        VK_CHECK_RESULT(vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount,
                                                             details.formats.data))
    }

    U32 presentModeCount;
    VK_CHECK_RESULT(
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr))

    if (presentModeCount != 0)
    {
        details.presentModes = BufferAlloc<VkPresentModeKHR>(arena, presentModeCount);
        VK_CHECK_RESULT(vkGetPhysicalDeviceSurfacePresentModesKHR(
            device, surface, &presentModeCount, details.presentModes.data))
    }

    return details;
}

// ~mgj: Camera functions
static void
VK_FrustumPlanesCalculate(VK_Frustum* out_frustum, const glm::mat4 matrix)
{
    out_frustum->planes[VK_PlaneType_Left].x = matrix[0].w + matrix[0].x;
    out_frustum->planes[VK_PlaneType_Left].y = matrix[1].w + matrix[1].x;
    out_frustum->planes[VK_PlaneType_Left].z = matrix[2].w + matrix[2].x;
    out_frustum->planes[VK_PlaneType_Left].w = matrix[3].w + matrix[3].x;

    out_frustum->planes[VK_PlaneType_Right].x = matrix[0].w - matrix[0].x;
    out_frustum->planes[VK_PlaneType_Right].y = matrix[1].w - matrix[1].x;
    out_frustum->planes[VK_PlaneType_Right].z = matrix[2].w - matrix[2].x;
    out_frustum->planes[VK_PlaneType_Right].w = matrix[3].w - matrix[3].x;

    out_frustum->planes[VK_PlaneType_Top].x = matrix[0].w - matrix[0].y;
    out_frustum->planes[VK_PlaneType_Top].y = matrix[1].w - matrix[1].y;
    out_frustum->planes[VK_PlaneType_Top].z = matrix[2].w - matrix[2].y;
    out_frustum->planes[VK_PlaneType_Top].w = matrix[3].w - matrix[3].y;

    out_frustum->planes[VK_PlaneType_Btm].x = matrix[0].w + matrix[0].y;
    out_frustum->planes[VK_PlaneType_Btm].y = matrix[1].w + matrix[1].y;
    out_frustum->planes[VK_PlaneType_Btm].z = matrix[2].w + matrix[2].y;
    out_frustum->planes[VK_PlaneType_Btm].w = matrix[3].w + matrix[3].y;

    out_frustum->planes[VK_PlaneType_Back].x = matrix[0].w + matrix[0].z;
    out_frustum->planes[VK_PlaneType_Back].y = matrix[1].w + matrix[1].z;
    out_frustum->planes[VK_PlaneType_Back].z = matrix[2].w + matrix[2].z;
    out_frustum->planes[VK_PlaneType_Back].w = matrix[3].w + matrix[3].z;

    out_frustum->planes[VK_PlaneType_Front].x = matrix[0].w - matrix[0].z;
    out_frustum->planes[VK_PlaneType_Front].y = matrix[1].w - matrix[1].z;
    out_frustum->planes[VK_PlaneType_Front].z = matrix[2].w - matrix[2].z;
    out_frustum->planes[VK_PlaneType_Front].w = matrix[3].w - matrix[3].z;

    for (size_t i = 0; i < ArrayCount(out_frustum->planes); i++)
    {
        float length = sqrtf(out_frustum->planes[i].x * out_frustum->planes[i].x +
                             out_frustum->planes[i].y * out_frustum->planes[i].y +
                             out_frustum->planes[i].z * out_frustum->planes[i].z);
        out_frustum->planes[i] /= length;
    }
}

static void
VK_ImageViewResourceDestroy(VK_ImageViewResource image_view_resource)
{
    vkDestroyImageView(image_view_resource.device, image_view_resource.image_view, 0);
}

static void
VK_ImageAllocationDestroy(VmaAllocator allocator, VK_ImageAllocation image_alloc)
{
    vmaDestroyImage(allocator, image_alloc.image, image_alloc.allocation);
}

static void
VK_ImageResourceDestroy(VmaAllocator allocator, VK_ImageResource image)
{
    VK_ImageViewResourceDestroy(image.image_view_resource);
    VK_ImageAllocationDestroy(allocator, image.image_alloc);
}

static VK_BufferAllocation
VK_BufferAllocationCreate(VmaAllocator allocator, VkDeviceSize size,
                          VkBufferUsageFlags buffer_usage, VmaAllocationCreateInfo vma_info)
{
    VK_BufferAllocation buffer = {};
    buffer.size = size;
    if (size == 0)
    {
        buffer.size = 64; // this avoids errors when creating buffers with zero size
    }

    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = buffer.size;
    bufferInfo.usage = buffer_usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkResult res = vmaCreateBuffer(allocator, &bufferInfo, &vma_info, &buffer.buffer,
                                   &buffer.allocation, nullptr);
    if (res != VK_SUCCESS)
    {
        exit_with_error("Failed to create buffer");
    }

    return buffer;
}

static void
VK_BufferReadbackCreate(VmaAllocator allocator, VkDeviceSize size, VkBufferUsageFlags buffer_usage,
                        VK_BufferReadback* out_buffer_readback)
{
    VkBufferCreateInfo bufCreateInfo = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufCreateInfo.size = size;
    bufCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | buffer_usage;

    VmaAllocationCreateInfo allocCreateInfo = {};
    allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocCreateInfo.flags =
        VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VkBuffer buf;
    VmaAllocation alloc;
    VmaAllocationInfo allocInfo;
    VK_CHECK_RESULT(
        vmaCreateBuffer(allocator, &bufCreateInfo, &allocCreateInfo, &buf, &alloc, &allocInfo));

    out_buffer_readback->mapped_ptr = allocInfo.pMappedData;
    out_buffer_readback->buffer_alloc = {.buffer = buf, .allocation = alloc, .size = size};
}

static void
VK_BufferReadbackDestroy(VmaAllocator allocator, VK_BufferReadback* out_buffer_readback)
{
    vmaDestroyBuffer(allocator, out_buffer_readback->buffer_alloc.buffer,
                     out_buffer_readback->buffer_alloc.allocation);
}

// inspiration:
// https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/usage_patterns.html
static VK_BufferAllocationMapped
VK_BufferMappedCreate(VmaAllocator allocator, VkDeviceSize size, VkBufferUsageFlags buffer_usage)
{
    Arena* arena = ArenaAlloc();

    VmaAllocationCreateInfo vma_info = {0};
    vma_info.usage = VMA_MEMORY_USAGE_AUTO;
    vma_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                     VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT |
                     VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VK_BufferAllocation buffer = VK_BufferAllocationCreate(
        allocator, size, buffer_usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, vma_info);

    VkMemoryPropertyFlags mem_prop_flags;
    vmaGetAllocationMemoryProperties(allocator, buffer.allocation, &mem_prop_flags);

    VmaAllocationInfo alloc_info;
    vmaGetAllocationInfo(allocator, buffer.allocation, &alloc_info);

    VK_BufferAllocationMapped mapped_buffer = {.buffer_alloc = buffer,
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
            VK_BufferAllocationCreate(allocator, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, vma_info);
    }

    return mapped_buffer;
}

static void
VK_BufferMappedUpdate(VkCommandBuffer cmd_buffer, VmaAllocator allocator,
                      VK_BufferAllocationMapped mapped_buffer)
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

static VK_ImageViewResource
VK_ImageViewResourceCreate(VkDevice device, VkImage image, VkFormat format,
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
    VK_CHECK_RESULT(vkCreateImageView(device, &viewInfo, nullptr, &view));

    return {.image_view = view, .device = device};
}

static VK_ImageAllocation
VK_ImageAllocationCreate(VmaAllocator allocator, U32 width, U32 height,
                         VkSampleCountFlagBits numSamples, VkFormat format, VkImageTiling tiling,
                         VkImageUsageFlags usage, U32 mipmap_level,
                         VmaAllocationCreateInfo vma_info)
{
    VK_ImageAllocation image_alloc = {0};
    VkExtent3D extent = {width, height, 1};

    VkImageCreateInfo image_create_info = {};
    image_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_create_info.imageType = VK_IMAGE_TYPE_2D;
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
    VK_CHECK_RESULT(vmaCreateImage(allocator, &image_create_info, &vma_info, &image_alloc.image,
                                   &image_alloc.allocation, &alloc_info))
    image_alloc.size = alloc_info.size;
    image_alloc.extent = extent;
    return image_alloc;
}

static void
VK_ColorResourcesCreate(VK_Context* vk_ctx, vk_SwapchainResources* swapchain_resources)
{
    VkFormat colorFormat = swapchain_resources->color_format;

    VmaAllocationCreateInfo vma_info = {
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
    };

    VK_ImageAllocation image_alloc = VK_ImageAllocationCreate(
        vk_ctx->allocator, swapchain_resources->swapchain_extent.width,
        swapchain_resources->swapchain_extent.height, vk_ctx->msaa_samples, colorFormat,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, 1, vma_info);

    VK_ImageViewResource color_image_view = VK_ImageViewResourceCreate(
        vk_ctx->device, image_alloc.image, colorFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1);

    swapchain_resources->color_image_resource = {.image_alloc = image_alloc,
                                                 .image_view_resource = color_image_view};
}
static void
VK_SwapChainImageResourceCreate(VkDevice device, vk_SwapchainResources* swapchain_resources,
                                U32 image_count)
{
    ScratchScope scratch = ScratchScope(0, 0);

    swapchain_resources->image_resources =
        BufferAlloc<VK_ImageSwapchainResource>(swapchain_resources->arena, image_count);

    VkImage* images = PushArray(scratch.arena, VkImage, image_count);
    if (vkGetSwapchainImagesKHR(device, swapchain_resources->swapchain, &image_count, images) !=
        VK_SUCCESS)
    {
        exit_with_error("failed to get swapchain images!");
    }

    for (uint32_t i = 0; i < image_count; i++)
    {
        VK_ImageViewResource image_view_resource = VK_ImageViewResourceCreate(
            device, images[i], swapchain_resources->swapchain_image_format,
            VK_IMAGE_ASPECT_COLOR_BIT, 1);

        swapchain_resources->image_resources.data[i] = {.image = images[i],
                                                        .image_view_resource = image_view_resource};
    }
}

// ~mgj: Swapchain functions
static VkSurfaceFormatKHR
VK_ChooseSwapSurfaceFormat(Buffer<VkSurfaceFormatKHR> availableFormats)
{
    for (U32 i = 0; i < availableFormats.size; i++)
    {
        if (availableFormats.data[i].format == VK_FORMAT_R8G8B8A8_SRGB &&
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
VK_ChooseSwapExtent(Vec2U32 framebuffer_dim, const VkSurfaceCapabilitiesKHR& capabilities)
{
    if (capabilities.currentExtent.width != UINT32_MAX)
    {
        return capabilities.currentExtent;
    }
    else
    {
        VkExtent2D actualExtent = {0, 0};

        actualExtent.width = Clamp(capabilities.minImageExtent.width, framebuffer_dim.x,
                                   capabilities.maxImageExtent.width);
        actualExtent.height = Clamp(capabilities.minImageExtent.height, framebuffer_dim.y,
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
static U32
VK_SwapChainImageCountGet(VkDevice device, vk_SwapchainResources* swapchain_resources)
{
    U32 imageCount = {0};
    if (vkGetSwapchainImagesKHR(device, swapchain_resources->swapchain, &imageCount, nullptr) !=
        VK_SUCCESS)
    {
        exit_with_error("failed to get swapchain image count!");
    }
    return imageCount;
}

static vk_SwapchainResources*
VK_SwapChainCreate(VK_Context* vk_ctx, Vec2U32 framebuffer_dim)
{
    Arena* arena = ArenaAlloc();
    vk_SwapchainResources* swapchain_resources = PushStruct(arena, vk_SwapchainResources);
    swapchain_resources->arena = arena;

    VK_SwapChainSupportDetails swapchain_details =
        VK_QuerySwapChainSupport(arena, vk_ctx->physical_device, vk_ctx->surface);

    VkSurfaceFormatKHR surface_format = VK_ChooseSwapSurfaceFormat(swapchain_details.formats);
    VkPresentModeKHR present_mode = VK_ChooseSwapPresentMode(swapchain_details.presentModes);
    VkExtent2D swapchain_extent =
        VK_ChooseSwapExtent(framebuffer_dim, swapchain_details.capabilities);

    U32 image_count = swapchain_details.capabilities.minImageCount + 1;

    if (swapchain_details.capabilities.maxImageCount > 0 &&
        image_count > swapchain_details.capabilities.maxImageCount)
    {
        image_count = swapchain_details.capabilities.maxImageCount;
    }

    VK_QueueFamilyIndices queueFamilyIndices = vk_ctx->queue_family_indices;
    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = vk_ctx->surface;
    createInfo.minImageCount = image_count;
    createInfo.imageFormat = surface_format.format;
    createInfo.imageColorSpace = surface_format.colorSpace;
    createInfo.imageExtent = swapchain_extent;
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

    createInfo.preTransform = swapchain_details.capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = present_mode;
    createInfo.clipped = VK_TRUE;
    // TODO: It is possible to specify the old swap chain to be replaced by a new
    // one
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    VkSwapchainKHR swapchain;
    VK_CHECK_RESULT(vkCreateSwapchainKHR(vk_ctx->device, &createInfo, nullptr, &swapchain));
    swapchain_resources->swapchain = swapchain;
    swapchain_resources->swapchain_extent = swapchain_extent;
    swapchain_resources->swapchain_image_format = surface_format.format;
    swapchain_resources->color_format = surface_format.format;
    swapchain_resources->swapchain_support = swapchain_details;
    swapchain_resources->present_mode = present_mode;
    swapchain_resources->surface_format = surface_format;
    U32 swapchain_image_count = VK_SwapChainImageCountGet(vk_ctx->device, swapchain_resources);
    swapchain_resources->image_count = swapchain_image_count;

    VK_SwapChainImageResourceCreate(vk_ctx->device, swapchain_resources, swapchain_image_count);

    VK_ColorResourcesCreate(vk_ctx, swapchain_resources);
    VK_DepthResourcesCreate(vk_ctx, swapchain_resources);
    ObjectIdImageResourceCreate(swapchain_resources, image_count);

    swapchain_resources->image_available_semaphores =
        BufferAlloc<VkSemaphore>(vk_ctx->arena, image_count);
    swapchain_resources->render_finished_semaphores =
        BufferAlloc<VkSemaphore>(vk_ctx->arena, image_count);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    for (U32 i = 0; i < image_count; i++)
    {
        if ((vkCreateSemaphore(vk_ctx->device, &semaphoreInfo, nullptr,
                               &swapchain_resources->image_available_semaphores.data[i]) !=
             VK_SUCCESS) ||
            (vkCreateSemaphore(vk_ctx->device, &semaphoreInfo, nullptr,
                               &swapchain_resources->render_finished_semaphores.data[i]) !=
             VK_SUCCESS))
        {
            exit_with_error("failed to create semaphores for swapchain images!");
        }
    }

    return swapchain_resources;
}

static void
VK_SwapChainCleanup(VkDevice device, VmaAllocator allocator,
                    vk_SwapchainResources* swapchain_resources)
{
    for (U32 i = 0; i < swapchain_resources->image_count; i++)
    {
        vkDestroySemaphore(device, swapchain_resources->image_available_semaphores.data[i],
                           nullptr);
        vkDestroySemaphore(device, swapchain_resources->render_finished_semaphores.data[i],
                           nullptr);
    }

    VK_ColorResourcesCleanup(allocator, swapchain_resources->color_image_resource);
    VK_DepthResourcesCleanup(allocator, swapchain_resources->depth_image_resource);
    ObjectIdResourcesCleanup();

    for (size_t i = 0; i < swapchain_resources->image_resources.size; i++)
    {
        VK_ImageViewResourceDestroy(
            swapchain_resources->image_resources.data[i].image_view_resource);
        swapchain_resources->image_resources.data[i].image_view_resource = {0};
    }

    vkDestroySwapchainKHR(device, swapchain_resources->swapchain, nullptr);
    swapchain_resources->swapchain = VK_NULL_HANDLE;
    ArenaRelease(swapchain_resources->arena);
}

static void
VK_RecreateSwapChain(Vec2U32 framebuffer_dim, VK_Context* vk_ctx)
{
    VK_CHECK_RESULT(vkDeviceWaitIdle(vk_ctx->device));

    VK_SyncObjectsDestroy(vk_ctx);
    VK_SwapChainCleanup(vk_ctx->device, vk_ctx->allocator, vk_ctx->swapchain_resources);
    vk_ctx->swapchain_resources = 0;
    vk_ctx->swapchain_resources = VK_SwapChainCreate(vk_ctx, framebuffer_dim);
    vk_sync_objects_create(vk_ctx);
}

// Samplers helpers
static VkSampler
VK_SamplerCreate(VkDevice device, VkSamplerCreateInfo* sampler_info)
{
    VkSampler sampler;
    if (vkCreateSampler(device, sampler_info, nullptr, &sampler) != VK_SUCCESS)
    {
        exit_with_error("failed to create texture sampler!");
    }
    return sampler;
}

// ~mgj: Descriptor Related Functions
static void
VK_DescriptorPoolCreate(VK_Context* vk_ctx)
{
    VkDescriptorPoolSize pool_sizes[] = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 6}, // 2 for both camera buffer and terrain buffer
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 20},
    };
    U32 pool_size_count = ArrayCount(pool_sizes);

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = pool_size_count;
    poolInfo.pPoolSizes = pool_sizes;

    poolInfo.maxSets = 20;

    if (vkCreateDescriptorPool(vk_ctx->device, &poolInfo, nullptr, &vk_ctx->descriptor_pool) !=
        VK_SUCCESS)
    {
        exit_with_error("failed to create descriptor pool!");
    }
}
static VkDescriptorSetLayout
VK_DescriptorSetLayoutCreate(VkDevice device, VkDescriptorSetLayoutBinding* bindings,
                             U32 binding_count)
{
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = binding_count;
    layoutInfo.pBindings = bindings;

    VkDescriptorSetLayout desc_set_layout;
    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &desc_set_layout) != VK_SUCCESS)
    {
        exit_with_error("failed to create descriptor set layout!");
    }

    return desc_set_layout;
}

static VkDescriptorSet
VK_DescriptorSetCreate(Arena* arena, VkDevice device, VkDescriptorPool desc_pool,
                       VkDescriptorSetLayout desc_set_layout, VkImageView image_view,
                       VkSampler sampler)
{
    ScratchScope scratch = ScratchScope(&arena, 1);

    VkDescriptorSetLayout layouts[] = {desc_set_layout};
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = desc_pool;
    allocInfo.descriptorSetCount = ArrayCount(layouts);
    allocInfo.pSetLayouts = layouts;

    VkDescriptorSet desc_set;
    VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &desc_set));

    VkDescriptorImageInfo image_info{};
    image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    image_info.imageView = image_view;
    image_info.sampler = sampler;

    VkWriteDescriptorSet texture_sampler_desc{};
    texture_sampler_desc.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    texture_sampler_desc.dstSet = desc_set;
    texture_sampler_desc.dstBinding = 0;
    texture_sampler_desc.dstArrayElement = 0;
    texture_sampler_desc.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    texture_sampler_desc.descriptorCount = 1;
    texture_sampler_desc.pImageInfo = &image_info;

    VkWriteDescriptorSet descriptors[] = {texture_sampler_desc};

    vkUpdateDescriptorSets(device, ArrayCount(descriptors), descriptors, 0, nullptr);

    return desc_set;
}

static VkFilter
VkFilterFromFilter(R_Filter filter)
{
    switch (filter)
    {
        case R_Filter_Nearest: return VK_FILTER_NEAREST;
        case R_Filter_Linear: return VK_FILTER_LINEAR;
        default: AssertAlways(1);
    }
    return VK_FILTER_NEAREST;
}

static VkSamplerMipmapMode
VkSamplerMipmapModeFromMipMapMode(R_MipMapMode mip_map_mode)
{
    switch (mip_map_mode)
    {
        case R_MipMapMode_Nearest: return VK_SAMPLER_MIPMAP_MODE_NEAREST;
        case R_MipMapMode_Linear: return VK_SAMPLER_MIPMAP_MODE_LINEAR;
        default: AssertAlways(1);
    }
    return VK_SAMPLER_MIPMAP_MODE_NEAREST;
}

static VkSamplerAddressMode
VkSamplerAddressModeFromSamplerAddressMode(R_SamplerAddressMode address_mode)
{
    switch (address_mode)
    {
        case R_SamplerAddressMode_Repeat: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        case R_SamplerAddressMode_MirroredRepeat: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        case R_SamplerAddressMode_ClampToEdge: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        default: AssertAlways(1);
    }
    return VK_SAMPLER_ADDRESS_MODE_REPEAT;
}
// ~mgj: Sampler
static void
VK_SamplerCreateInfoFromSamplerInfo(R_SamplerInfo* sampler, VkSamplerCreateInfo* out_sampler_info)
{
    out_sampler_info->sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    out_sampler_info->magFilter = VkFilterFromFilter(sampler->mag_filter);
    out_sampler_info->minFilter = VkFilterFromFilter(sampler->min_filter);
    out_sampler_info->addressModeU =
        VkSamplerAddressModeFromSamplerAddressMode(sampler->address_mode_u);
    out_sampler_info->addressModeV =
        VkSamplerAddressModeFromSamplerAddressMode(sampler->address_mode_v);
    out_sampler_info->addressModeW = out_sampler_info->addressModeU;
    out_sampler_info->compareOp = VK_COMPARE_OP_NEVER;
    out_sampler_info->unnormalizedCoordinates = VK_FALSE;
    out_sampler_info->mipmapMode = VkSamplerMipmapModeFromMipMapMode(sampler->mip_map_mode);
    out_sampler_info->mipLodBias = 0.0f;
    out_sampler_info->minLod = 0.0f;
    out_sampler_info->maxLod = VK_LOD_CLAMP_NONE;
    out_sampler_info->borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
}

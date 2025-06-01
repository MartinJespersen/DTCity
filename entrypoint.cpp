
#include <cstdlib>
#include <cstring>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan_core.h>
// domain: hpp
#include "entrypoint.hpp"

// domain: cpp
#include "base/base_inc.cpp"
#include "ui/ui.cpp"

// profiler
#include "profiler/tracy/Tracy.hpp"
#include "profiler/tracy/TracyVulkan.hpp"

internal VkResult
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

internal void
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

C_LINKAGE void
InitContext()
{
    Context* ctx = GlobalContextGet();
    ctx->arena_permanent = (Arena*)arena_alloc();

    ThreadContextInit();
    InitWindow();
    VK_VulkanInit();
}

C_LINKAGE void
DeleteContext()
{
    VK_Cleanup();
    ThreadContextExit();
    Context* ctx = GlobalContextGet();
    ASSERT(ctx, "No Global Context found.");
}

internal void
VK_FramebufferResizeCallback(GLFWwindow* window, int width, int height)
{
    (void)width;
    (void)height;

    auto context = reinterpret_cast<Context*>(glfwGetWindowUserPointer(window));
    context->vulkanContext->framebuffer_resized = 1;
}

internal void
InitWindow()
{
    Context* ctx = GlobalContextGet();
    VulkanContext* vulkanContext = ctx->vulkanContext;

    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    vulkanContext->window = glfwCreateWindow(800, 600, "Vulkan", nullptr, nullptr);
    glfwSetWindowUserPointer(vulkanContext->window, ctx);
    glfwSetFramebufferSizeCallback(vulkanContext->window, VK_FramebufferResizeCallback);
}

internal void
VK_VulkanInit()
{
    Context* ctx = GlobalContextGet();
    Temp scratch = scratch_begin(0, 0);

    VulkanContext* vk_ctx = ctx->vulkanContext;
    vk_ctx->arena = arena_alloc();

    VK_CreateInstance(vk_ctx);
    VK_DebugMessengerSetup(vk_ctx);
    VK_SurfaceCreate(vk_ctx);
    VK_PhysicalDevicePick(vk_ctx);
    VK_LogicalDeviceCreate(scratch.arena, vk_ctx);
    SwapChainInfo swapChainInfo = VK_SwapChainCreate(scratch.arena, vk_ctx);
    U32 swapChainImageCount = VK_SwapChainImageCountGet(vk_ctx);
    vk_ctx->swapchain_images = BufferAlloc<VkImage>(vk_ctx->arena, (U64)swapChainImageCount);
    vk_ctx->swapchain_image_views =
        BufferAlloc<VkImageView>(vk_ctx->arena, (U64)swapChainImageCount);
    vk_ctx->swapchain_framebuffers =
        BufferAlloc<VkFramebuffer>(vk_ctx->arena, (U64)swapChainImageCount);

    VK_SwapChainImagesCreate(vk_ctx, swapChainInfo, swapChainImageCount);
    VK_SwapChainImageViewsCreate(vk_ctx);
    VK_CommandPoolCreate(vk_ctx);

    vk_ctx->color_image_view =
        createColorResources(vk_ctx->physical_device, vk_ctx->device,
                             vk_ctx->swapchain_image_format, vk_ctx->swapchain_extent,
                             vk_ctx->msaa_samples, vk_ctx->color_image, vk_ctx->color_image_memory);

    VK_CommandBuffersCreate(ctx);
    VK_SyncObjectsCreate(vk_ctx);
    RenderPassCreate();

    TerrainInit();
    createFramebuffers(vk_ctx, vk_ctx->vk_renderpass);
    scratch_end(scratch);
}

internal void
VK_Cleanup()
{
    Context* ctx = GlobalContextGet();
    VulkanContext* vulkanContext = ctx->vulkanContext;

    vkDeviceWaitIdle(vulkanContext->device);

    if (vulkanContext->enable_validation_layers)
    {
        DestroyDebugUtilsMessengerEXT(vulkanContext->instance, vulkanContext->debug_messenger,
                                      nullptr);
    }
    VK_SwapChainCleanup(vulkanContext);

#ifdef PROFILING_ENABLE
    for (U32 i = 0; i < ctx->profilingContext->tracyContexts.size; i++)
    {
        TracyVkDestroy(ctx->profilingContext->tracyContexts.data[i]);
    }
#endif
    vkDestroyCommandPool(vulkanContext->device, vulkanContext->command_pool, nullptr);

    vkDestroySurfaceKHR(vulkanContext->instance, vulkanContext->surface, nullptr);

    for (U32 i = 0; i < (U32)vulkanContext->MAX_FRAMES_IN_FLIGHT; i++)
    {
        vkDestroySemaphore(vulkanContext->device, vulkanContext->render_finished_semaphores.data[i],
                           nullptr);
        vkDestroySemaphore(vulkanContext->device, vulkanContext->image_available_semaphores.data[i],
                           nullptr);
        vkDestroyFence(vulkanContext->device, vulkanContext->in_flight_fences.data[i], nullptr);
    }

    vkDestroyDevice(vulkanContext->device, nullptr);
    vkDestroyInstance(vulkanContext->instance, nullptr);

    glfwDestroyWindow(vulkanContext->window);

    glfwTerminate();
}

internal void
VK_ColorResourcesCleanup(VulkanContext* vulkanContext)
{
    vkDestroyImageView(vulkanContext->device, vulkanContext->color_image_view, nullptr);
    vkDestroyImage(vulkanContext->device, vulkanContext->color_image, nullptr);
    vkFreeMemory(vulkanContext->device, vulkanContext->color_image_memory, nullptr);
}

internal void
VK_SwapChainCleanup(VulkanContext* vulkanContext)
{
    VK_ColorResourcesCleanup(vulkanContext);

    for (size_t i = 0; i < vulkanContext->swapchain_framebuffers.size; i++)
    {
        vkDestroyFramebuffer(vulkanContext->device, vulkanContext->swapchain_framebuffers.data[i],
                             nullptr);
    }

    for (size_t i = 0; i < vulkanContext->swapchain_image_views.size; i++)
    {
        vkDestroyImageView(vulkanContext->device, vulkanContext->swapchain_image_views.data[i],
                           nullptr);
    }

    vkDestroySwapchainKHR(vulkanContext->device, vulkanContext->swapchain, nullptr);
}

internal void
VK_SyncObjectsCreate(VulkanContext* vulkanContext)
{
    vulkanContext->image_available_semaphores =
        BufferAlloc<VkSemaphore>(vulkanContext->arena, vulkanContext->MAX_FRAMES_IN_FLIGHT);
    vulkanContext->render_finished_semaphores =
        BufferAlloc<VkSemaphore>(vulkanContext->arena, vulkanContext->MAX_FRAMES_IN_FLIGHT);
    vulkanContext->in_flight_fences =
        BufferAlloc<VkFence>(vulkanContext->arena, vulkanContext->MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (U32 i = 0; i < (U32)vulkanContext->MAX_FRAMES_IN_FLIGHT; i++)
    {
        if (vkCreateSemaphore(vulkanContext->device, &semaphoreInfo, nullptr,
                              &vulkanContext->image_available_semaphores.data[i]) != VK_SUCCESS ||
            vkCreateSemaphore(vulkanContext->device, &semaphoreInfo, nullptr,
                              &vulkanContext->render_finished_semaphores.data[i]) != VK_SUCCESS ||
            vkCreateFence(vulkanContext->device, &fenceInfo, nullptr,
                          &vulkanContext->in_flight_fences.data[i]) != VK_SUCCESS)
        {
            exitWithError("failed to create synchronization objects for a frame!");
        }
    }
}

internal void
VK_CommandBuffersCreate(Context* context)
{
    VulkanContext* vulkanContext = context->vulkanContext;

    vulkanContext->command_buffers = BufferAlloc<VkCommandBuffer>(
        vulkanContext->arena, vulkanContext->swapchain_framebuffers.size);
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = vulkanContext->command_pool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = (uint32_t)vulkanContext->command_buffers.size;

    if (vkAllocateCommandBuffers(vulkanContext->device, &allocInfo,
                                 vulkanContext->command_buffers.data) != VK_SUCCESS)
    {
        exitWithError("failed to allocate command buffers!");
    }

#ifdef PROFILING_ENABLE
    context->profilingContext->tracyContexts =
        BufferAlloc<TracyVkCtx>(vulkanContext->arena, vulkanContext->swapchain_framebuffers.size);
    for (U32 i = 0; i < vulkanContext->command_buffers.size; i++)
    {
        context->profilingContext->tracyContexts.data[i] =
            TracyVkContext(vulkanContext->physical_device, vulkanContext->device,
                           vulkanContext->graphics_queue, vulkanContext->command_buffers.data[i]);
    }
#endif
}

internal void
VK_CommandPoolCreate(VulkanContext* vulkanContext)
{
    QueueFamilyIndices queueFamilyIndices = vulkanContext->queue_family_indices;

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamilyIndex;

    if (vkCreateCommandPool(vulkanContext->device, &poolInfo, nullptr,
                            &vulkanContext->command_pool) != VK_SUCCESS)
    {
        exitWithError("failed to create command pool!");
    }
}

internal void
VK_SwapChainImageViewsCreate(VulkanContext* vulkanContext)
{
    for (uint32_t i = 0; i < vulkanContext->swapchain_images.size; i++)
    {
        vulkanContext->swapchain_image_views.data[i] =
            createImageView(vulkanContext->device, vulkanContext->swapchain_images.data[i],
                            vulkanContext->swapchain_image_format);
    }
}

internal void
VK_SwapChainImagesCreate(VulkanContext* vulkanContext, SwapChainInfo swapChainInfo, U32 imageCount)
{
    vkGetSwapchainImagesKHR(vulkanContext->device, vulkanContext->swapchain, &imageCount,
                            vulkanContext->swapchain_images.data);

    vulkanContext->swapchain_image_format = swapChainInfo.surfaceFormat.format;
    vulkanContext->swapchain_extent = swapChainInfo.extent;
}

internal U32
VK_SwapChainImageCountGet(VulkanContext* vulkanContext)
{
    U32 imageCount = {0};
    vkGetSwapchainImagesKHR(vulkanContext->device, vulkanContext->swapchain, &imageCount, nullptr);
    return imageCount;
}

internal SwapChainInfo
VK_SwapChainCreate(Arena* arena, VulkanContext* vulkanContext)
{
    SwapChainInfo swapChainInfo = {0};

    swapChainInfo.swapChainSupport =
        querySwapChainSupport(arena, vulkanContext, vulkanContext->physical_device);

    swapChainInfo.surfaceFormat =
        VK_ChooseSwapSurfaceFormat(swapChainInfo.swapChainSupport.formats);
    swapChainInfo.presentMode =
        VK_ChooseSwapPresentMode(swapChainInfo.swapChainSupport.presentModes);
    swapChainInfo.extent =
        VK_ChooseSwapExtent(vulkanContext, swapChainInfo.swapChainSupport.capabilities);

    U32 imageCount = swapChainInfo.swapChainSupport.capabilities.minImageCount + 1;

    if (swapChainInfo.swapChainSupport.capabilities.maxImageCount > 0 &&
        imageCount > swapChainInfo.swapChainSupport.capabilities.maxImageCount)
    {
        imageCount = swapChainInfo.swapChainSupport.capabilities.maxImageCount;
    }

    QueueFamilyIndices queueFamilyIndices = vulkanContext->queue_family_indices;
    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = vulkanContext->surface;

    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = swapChainInfo.surfaceFormat.format;
    createInfo.imageColorSpace = swapChainInfo.surfaceFormat.colorSpace;
    createInfo.imageExtent = swapChainInfo.extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    if (queueFamilyIndices.graphicsFamilyIndex != queueFamilyIndices.presentFamilyIndex)
    {
        U32 queueFamilyIndicesSame[] = {vulkanContext->queue_family_indices.graphicsFamilyIndex,
                                        vulkanContext->queue_family_indices.presentFamilyIndex};
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
    // It is possible to specify the old swap chain to be replaced by a new one
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(vulkanContext->device, &createInfo, nullptr,
                             &vulkanContext->swapchain) != VK_SUCCESS)
    {
        exitWithError("failed to create swap chain!");
    }

    return swapChainInfo;
}

internal void
VK_SurfaceCreate(VulkanContext* vulkanContext)
{
    if (glfwCreateWindowSurface(vulkanContext->instance, vulkanContext->window, nullptr,
                                &vulkanContext->surface) != VK_SUCCESS)
    {
        exitWithError("failed to create window surface!");
    }
}

internal char**
VK_StrArrFromStr8Buffer(Arena* arena, String8* buffer, U64 count)
{
    char** arr = push_array(arena, char*, count);

    for (U32 i = 0; i < count; i++)
    {
        arr[i] = (char*)buffer[i].str;
    }
    return arr;
}

internal void
VK_CreateInstance(VulkanContext* vk_ctx)
{
    Temp scratch = scratch_begin(0, 0);
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
    appInfo.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    Buffer<String8> extensions = VK_RequiredExtensionsGet(vk_ctx);

    createInfo.enabledExtensionCount = (U32)extensions.size;
    createInfo.ppEnabledExtensionNames =
        VK_StrArrFromStr8Buffer(scratch.arena, extensions.data, extensions.size);

    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    if (vk_ctx->enable_validation_layers)
    {
        createInfo.enabledLayerCount = (U32)ArrayCount(vk_ctx->validation_layers);
        createInfo.ppEnabledLayerNames = vk_ctx->validation_layers;

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

    scratch_end(scratch);
}

internal void
VK_LogicalDeviceCreate(Arena* arena, VulkanContext* vulkanContext)
{
    QueueFamilyIndices queueFamilyIndicies = vulkanContext->queue_family_indices;

    U32 uniqueQueueFamiliesCount = 1;
    if (queueFamilyIndicies.graphicsFamilyIndex != queueFamilyIndicies.presentFamilyIndex)
    {
        uniqueQueueFamiliesCount++;
    }

    U32 uniqueQueueFamilies[] = {queueFamilyIndicies.graphicsFamilyIndex,
                                 queueFamilyIndicies.presentFamilyIndex};

    VkDeviceQueueCreateInfo* queueCreateInfos =
        push_array(arena, VkDeviceQueueCreateInfo, uniqueQueueFamiliesCount);
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

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;

    createInfo.pQueueCreateInfos = queueCreateInfos;

    createInfo.queueCreateInfoCount = uniqueQueueFamiliesCount;

    createInfo.pEnabledFeatures = &deviceFeatures;

    createInfo.enabledExtensionCount = (U32)ArrayCount(vulkanContext->device_extensions);
    createInfo.ppEnabledExtensionNames = vulkanContext->device_extensions;

    // NOTE: This if statement is no longer necessary on newer versions
    if (vulkanContext->enable_validation_layers)
    {
        createInfo.enabledLayerCount = (U32)ArrayCount(vulkanContext->validation_layers);
        createInfo.ppEnabledLayerNames = vulkanContext->validation_layers;
    }
    else
    {
        createInfo.enabledLayerCount = 0;
    }

    if (vkCreateDevice(vulkanContext->physical_device, &createInfo, nullptr,
                       &vulkanContext->device) != VK_SUCCESS)
    {
        exitWithError("failed to create logical device!");
    }

    vkGetDeviceQueue(vulkanContext->device, queueFamilyIndicies.graphicsFamilyIndex, 0,
                     &vulkanContext->graphics_queue);
    vkGetDeviceQueue(vulkanContext->device, queueFamilyIndicies.presentFamilyIndex, 0,
                     &vulkanContext->present_queue);
}

internal void
VK_PhysicalDevicePick(VulkanContext* vulkanContext)
{
    Temp scratch = scratch_begin(0, 0);

    vulkanContext->physical_device = VK_NULL_HANDLE;

    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(vulkanContext->instance, &deviceCount, nullptr);

    if (deviceCount == 0)
    {
        exitWithError("failed to find GPUs with Vulkan support!");
    }

    VkPhysicalDevice* devices = push_array(scratch.arena, VkPhysicalDevice, deviceCount);
    vkEnumeratePhysicalDevices(vulkanContext->instance, &deviceCount, devices);

    for (U32 i = 0; i < deviceCount; i++)
    {
        QueueFamilyIndexBits familyIndexBits = QueueFamiliesFind(vulkanContext, devices[i]);
        if (VK_IsDeviceSuitable(vulkanContext, devices[i], familyIndexBits))
        {
            vulkanContext->physical_device = devices[i];
            vulkanContext->msaa_samples = VK_MaxUsableSampleCountGet(devices[i]);
            vulkanContext->queue_family_indices = QueueFamilyIndicesFromBitFields(familyIndexBits);
            break;
        }
    }

    if (vulkanContext->physical_device == VK_NULL_HANDLE)
    {
        exitWithError("failed to find a suitable GPU!");
    }

    scratch_end(scratch);
}

internal bool
VK_IsDeviceSuitable(VulkanContext* vulkanContext, VkPhysicalDevice device,
                    QueueFamilyIndexBits indexBits)
{
    // NOTE: This is where you would implement your own checks to see if the
    // device is suitable for your needs

    bool extensionsSupported = VK_CheckDeviceExtensionSupport(vulkanContext, device);

    bool swapChainAdequate = false;
    if (extensionsSupported)
    {
        SwapChainSupportDetails swapChainDetails =
            querySwapChainSupport(vulkanContext->arena, vulkanContext, device);
        swapChainAdequate = swapChainDetails.formats.size && swapChainDetails.presentModes.size;
    }

    VkPhysicalDeviceFeatures supportedFeatures;
    vkGetPhysicalDeviceFeatures(device, &supportedFeatures);

    return QueueFamilyIsComplete(indexBits) && extensionsSupported && swapChainAdequate &&
           supportedFeatures.samplerAnisotropy;
}

internal bool
VK_CheckValidationLayerSupport(VulkanContext* vulkanContext)
{
    Temp scratch = scratch_begin(0, 0);
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

    VkLayerProperties* availableLayers = push_array(scratch.arena, VkLayerProperties, layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers);

    bool layerFound = false;
    for (const char* layerName : vulkanContext->validation_layers)
    {
        layerFound = false;
        for (U32 i = 0; i < layerCount; i++)
        {
            if (strcmp(layerName, availableLayers[i].layerName) == 0)
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

    scratch_end(scratch);
    return layerFound;
}

internal Buffer<String8>
VK_RequiredExtensionsGet(VulkanContext* vulkanContext)
{
    U32 glfwExtensionCount = 0;
    const char** glfwExtensions;
    glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    U32 extensionCount = glfwExtensionCount;
    if (vulkanContext->enable_validation_layers)
    {
        extensionCount++;
    }

    Buffer<String8> extensions = BufferAlloc<String8>(vulkanContext->arena, extensionCount);

    for (U32 i = 0; i < glfwExtensionCount; i++)
    {
        extensions.data[i] = push_str8f(vulkanContext->arena, glfwExtensions[i]);
    }

    if (vulkanContext->enable_validation_layers)
    {
        extensions.data[glfwExtensionCount] =
            push_str8f(vulkanContext->arena, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    return extensions;
}

internal VKAPI_ATTR VkBool32 VKAPI_CALL
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

internal void
VK_DebugMessengerSetup(VulkanContext* vulkanContext)
{
    if (!vulkanContext->enable_validation_layers)
        return;
    VkDebugUtilsMessengerCreateInfoEXT createInfo;
    VK_PopulateDebugMessengerCreateInfo(createInfo);
    if (CreateDebugUtilsMessengerEXT(vulkanContext->instance, &createInfo, nullptr,
                                     &vulkanContext->debug_messenger) != VK_SUCCESS)
    {
        exitWithError("failed to set up debug messenger!");
    }
}

internal void
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

internal bool
VK_CheckDeviceExtensionSupport(VulkanContext* vulkanContext, VkPhysicalDevice device)
{
    Temp scratch = scratch_begin(0, 0);
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

    VkExtensionProperties* availableExtensions =
        push_array(scratch.arena, VkExtensionProperties, extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions);
    const U64 numberOfRequiredExtenstions = ArrayCount(vulkanContext->device_extensions);
    U64 numberOfRequiredExtenstionsLeft = numberOfRequiredExtenstions;
    for (U32 i = 0; i < extensionCount; i++)
    {
        for (U32 j = 0; j < numberOfRequiredExtenstions; j++)
        {
            if (CStrEqual(vulkanContext->device_extensions[j],
                          availableExtensions[i].extensionName))
            {
                numberOfRequiredExtenstionsLeft--;
                break;
            }
        }
    }

    return numberOfRequiredExtenstionsLeft == 0;
}

internal VkSurfaceFormatKHR
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

internal VkPresentModeKHR
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

internal VkExtent2D
VK_ChooseSwapExtent(VulkanContext* vulkanContext, const VkSurfaceCapabilitiesKHR& capabilities)
{
    if (capabilities.currentExtent.width != UINT32_MAX)
    {
        return capabilities.currentExtent;
    }
    else
    {
        int width, height;
        glfwGetFramebufferSize(vulkanContext->window, &width, &height);

        VkExtent2D actualExtent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};

        actualExtent.width = Clamp(actualExtent.width, capabilities.minImageExtent.width,
                                   capabilities.maxImageExtent.width);
        actualExtent.height = Clamp(actualExtent.height, capabilities.minImageExtent.height,
                                    capabilities.maxImageExtent.height);

        return actualExtent;
    }
}

internal VkSampleCountFlagBits
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

internal void
VK_CommandBufferRecord(U32 image_index, U32 current_frame)
{
    ZoneScoped;
    Temp scratch = scratch_begin(0, 0);
    Context* ctx = GlobalContextGet();

    VulkanContext* vk_ctx = ctx->vulkanContext;
    ProfilingContext* profilingContext = ctx->profilingContext;
    (void)profilingContext;

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = 0;                  // Optional
    beginInfo.pInheritanceInfo = nullptr; // Optional

    if (vkBeginCommandBuffer(vk_ctx->command_buffers.data[current_frame], &beginInfo) != VK_SUCCESS)
    {
        exitWithError("failed to begin recording command buffer!");
    }

    TracyVkCollect(profilingContext->tracyContexts.data[current_frame],
                   vk_ctx->command_buffers.data[current_frame]);

    Buffer<Buffer<Vertex>> buf_of_vert_buffers = BufferAlloc<Buffer<Vertex>>(scratch.arena, 1);
    buf_of_vert_buffers.data[0] = ctx->terrain->vertices;

    Buffer<Buffer<U32>> buf_of_indice_buffers = BufferAlloc<Buffer<U32>>(scratch.arena, 1);
    buf_of_indice_buffers.data[0] = ctx->terrain->indices;

    VK_BufferContextCreate(vk_ctx, &vk_ctx->vk_vertex_context, buf_of_vert_buffers,
                           VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    VK_BufferContextCreate(vk_ctx, &vk_ctx->vk_indice_context, buf_of_indice_buffers,
                           VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    UpdateTerrainTransform(
        ctx->terrain,
        Vec2F32{(F32)vk_ctx->swapchain_extent.width, (F32)vk_ctx->swapchain_extent.height},
        current_frame);
    TerrainRenderPassBegin(vk_ctx, ctx->terrain, image_index, current_frame);

    if (vkEndCommandBuffer(vk_ctx->command_buffers.data[current_frame]) != VK_SUCCESS)
    {
        exitWithError("failed to record command buffer!");
    }
    scratch_end(scratch);
}

C_LINKAGE void
DrawFrame()
{
    ZoneScoped;
    Context* context = GlobalContextGet();
    VulkanContext* vulkanContext = context->vulkanContext;

    {
        ZoneScopedN("Wait for frame");
        vkWaitForFences(vulkanContext->device, 1,
                        &vulkanContext->in_flight_fences.data[vulkanContext->current_frame],
                        VK_TRUE, UINT64_MAX);
    }

    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(
        vulkanContext->device, vulkanContext->swapchain, UINT64_MAX,
        vulkanContext->image_available_semaphores.data[vulkanContext->current_frame],
        VK_NULL_HANDLE, &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
    {
        VK_RecreateSwapChain(vulkanContext);
        return;
    }
    else if (result != VK_SUCCESS)
    {
        exitWithError("failed to acquire swap chain image!");
    }

    vkResetFences(vulkanContext->device, 1,
                  &vulkanContext->in_flight_fences.data[vulkanContext->current_frame]);
    vkResetCommandBuffer(vulkanContext->command_buffers.data[vulkanContext->current_frame], 0);

    VK_CommandBufferRecord(imageIndex, vulkanContext->current_frame);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    VkSemaphore waitSemaphores[] = {
        vulkanContext->image_available_semaphores.data[vulkanContext->current_frame]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &vulkanContext->command_buffers.data[vulkanContext->current_frame];

    VkSemaphore signalSemaphores[] = {
        vulkanContext->render_finished_semaphores.data[vulkanContext->current_frame]};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    if (vkQueueSubmit(vulkanContext->graphics_queue, 1, &submitInfo,
                      vulkanContext->in_flight_fences.data[vulkanContext->current_frame]) !=
        VK_SUCCESS)
    {
        exitWithError("failed to submit draw command buffer!");
    }

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;

    VkSwapchainKHR swapChains[] = {vulkanContext->swapchain};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &imageIndex;

    presentInfo.pResults = nullptr; // Optional

    result = vkQueuePresentKHR(vulkanContext->present_queue, &presentInfo);
    // TracyVkCollect(tracyContexts[currentFrame], commandBuffers[currentFrame]);
    FrameMark; // end of frame is assumed to be here
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR ||
        vulkanContext->framebuffer_resized)
    {
        vulkanContext->framebuffer_resized = 0;
        VK_RecreateSwapChain(vulkanContext);
    }
    else if (result != VK_SUCCESS)
    {
        exitWithError("failed to present swap chain image!");
    }

    vulkanContext->current_frame =
        (vulkanContext->current_frame + 1) % vulkanContext->MAX_FRAMES_IN_FLIGHT;
}

internal void
VK_RecreateSwapChain(VulkanContext* vk_ctx)
{
    Temp scratch = scratch_begin(0, 0);
    int width = 0, height = 0;
    glfwGetFramebufferSize(vk_ctx->window, &width, &height);
    while (width == 0 || height == 0)
    {
        glfwGetFramebufferSize(vk_ctx->window, &width, &height);
        glfwWaitEvents();
    }
    vkDeviceWaitIdle(vk_ctx->device);

    VK_SwapChainCleanup(vk_ctx);

    SwapChainInfo swapChainInfo = VK_SwapChainCreate(scratch.arena, vk_ctx);
    U32 swapChainImageCount = VK_SwapChainImageCountGet(vk_ctx);
    VK_SwapChainImagesCreate(vk_ctx, swapChainInfo, swapChainImageCount);

    VK_SwapChainImageViewsCreate(vk_ctx);
    vk_ctx->color_image_view =
        createColorResources(vk_ctx->physical_device, vk_ctx->device,
                             vk_ctx->swapchain_image_format, vk_ctx->swapchain_extent,
                             vk_ctx->msaa_samples, vk_ctx->color_image, vk_ctx->color_image_memory);

    createFramebuffers(vk_ctx, vk_ctx->vk_renderpass);
    scratch_end(scratch);
}
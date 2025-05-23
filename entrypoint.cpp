
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
    initWindow();
    VulkanInit();
}

C_LINKAGE void
DeleteContext()
{
    cleanup();
    ThreadContextExit();
    Context* ctx = GlobalContextGet();
    ASSERT(ctx, "No Global Context found.");
}

internal void
framebufferResizeCallback(GLFWwindow* window, int width, int height)
{
    (void)width;
    (void)height;

    auto context = reinterpret_cast<Context*>(glfwGetWindowUserPointer(window));
    context->vulkanContext->framebufferResized = 1;
}

internal void
initWindow()
{
    Context* ctx = GlobalContextGet();
    VulkanContext* vulkanContext = ctx->vulkanContext;

    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    vulkanContext->window = glfwCreateWindow(800, 600, "Vulkan", nullptr, nullptr);
    glfwSetWindowUserPointer(vulkanContext->window, ctx);
    glfwSetFramebufferSizeCallback(vulkanContext->window, framebufferResizeCallback);
}

internal void
VulkanInit()
{
    Context* ctx = GlobalContextGet();
    Temp scratch = scratch_begin(0, 0);

    VulkanContext* vulkanContext = ctx->vulkanContext;
    vulkanContext->arena = arena_alloc();

    createInstance(vulkanContext);
    setupDebugMessenger(vulkanContext);
    createSurface(vulkanContext);
    pickPhysicalDevice(vulkanContext);
    createLogicalDevice(scratch.arena, vulkanContext);
    SwapChainInfo swapChainInfo = SwapChainCreate(scratch.arena, vulkanContext);
    U32 swapChainImageCount = SwapChainImageCountGet(vulkanContext);
    vulkanContext->swapChainImages =
        BufferAlloc<VkImage>(vulkanContext->arena, (U64)swapChainImageCount);
    vulkanContext->swapChainImageViews =
        BufferAlloc<VkImageView>(vulkanContext->arena, (U64)swapChainImageCount);
    vulkanContext->swapChainFramebuffers =
        BufferAlloc<VkFramebuffer>(vulkanContext->arena, (U64)swapChainImageCount);

    SwapChainImagesCreate(vulkanContext, swapChainInfo, swapChainImageCount);
    SwapChainImageViewsCreate(vulkanContext);
    createCommandPool(vulkanContext);

    vulkanContext->resolutionInfo.offset = 0;
    vulkanContext->resolutionInfo.size = sizeof(float) * 2;

    vulkanContext->colorImageView = createColorResources(
        vulkanContext->physicalDevice, vulkanContext->device, vulkanContext->swapChainImageFormat,
        vulkanContext->swapChainExtent, vulkanContext->msaaSamples, vulkanContext->colorImage,
        vulkanContext->colorImageMemory);
    // TODO: missing framebuffer creation

    createCommandBuffers(ctx);
    createSyncObjects(vulkanContext);

    TerrainInit();

    scratch_end(scratch);
}

internal void
cleanup()
{
    Context* ctx = GlobalContextGet();
    VulkanContext* vulkanContext = ctx->vulkanContext;

    vkDeviceWaitIdle(vulkanContext->device);

    if (vulkanContext->enableValidationLayers)
    {
        DestroyDebugUtilsMessengerEXT(vulkanContext->instance, vulkanContext->debugMessenger,
                                      nullptr);
    }
    cleanupSwapChain(vulkanContext);

#ifdef PROFILING_ENABLE
    for (U32 i = 0; i < ctx->profilingContext->tracyContexts.size; i++)
    {
        TracyVkDestroy(ctx->profilingContext->tracyContexts.data[i]);
    }
#endif
    vkDestroyCommandPool(vulkanContext->device, vulkanContext->commandPool, nullptr);

    vkDestroySurfaceKHR(vulkanContext->instance, vulkanContext->surface, nullptr);

    for (U32 i = 0; i < (U32)vulkanContext->MAX_FRAMES_IN_FLIGHT; i++)
    {
        vkDestroySemaphore(vulkanContext->device, vulkanContext->renderFinishedSemaphores.data[i],
                           nullptr);
        vkDestroySemaphore(vulkanContext->device, vulkanContext->imageAvailableSemaphores.data[i],
                           nullptr);
        vkDestroyFence(vulkanContext->device, vulkanContext->inFlightFences.data[i], nullptr);
    }

    vkDestroyDevice(vulkanContext->device, nullptr);
    vkDestroyInstance(vulkanContext->instance, nullptr);

    glfwDestroyWindow(vulkanContext->window);

    glfwTerminate();
}

internal void
cleanupColorResources(VulkanContext* vulkanContext)
{
    vkDestroyImageView(vulkanContext->device, vulkanContext->colorImageView, nullptr);
    vkDestroyImage(vulkanContext->device, vulkanContext->colorImage, nullptr);
    vkFreeMemory(vulkanContext->device, vulkanContext->colorImageMemory, nullptr);
}

internal void
cleanupSwapChain(VulkanContext* vulkanContext)
{
    cleanupColorResources(vulkanContext);

    for (size_t i = 0; i < vulkanContext->swapChainFramebuffers.size; i++)
    {
        vkDestroyFramebuffer(vulkanContext->device, vulkanContext->swapChainFramebuffers.data[i],
                             nullptr);
    }

    for (size_t i = 0; i < vulkanContext->swapChainImageViews.size; i++)
    {
        vkDestroyImageView(vulkanContext->device, vulkanContext->swapChainImageViews.data[i],
                           nullptr);
    }

    vkDestroySwapchainKHR(vulkanContext->device, vulkanContext->swapChain, nullptr);
}

internal void
createSyncObjects(VulkanContext* vulkanContext)
{
    vulkanContext->imageAvailableSemaphores =
        BufferAlloc<VkSemaphore>(vulkanContext->arena, vulkanContext->MAX_FRAMES_IN_FLIGHT);
    vulkanContext->renderFinishedSemaphores =
        BufferAlloc<VkSemaphore>(vulkanContext->arena, vulkanContext->MAX_FRAMES_IN_FLIGHT);
    vulkanContext->inFlightFences =
        BufferAlloc<VkFence>(vulkanContext->arena, vulkanContext->MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (U32 i = 0; i < (U32)vulkanContext->MAX_FRAMES_IN_FLIGHT; i++)
    {
        if (vkCreateSemaphore(vulkanContext->device, &semaphoreInfo, nullptr,
                              &vulkanContext->imageAvailableSemaphores.data[i]) != VK_SUCCESS ||
            vkCreateSemaphore(vulkanContext->device, &semaphoreInfo, nullptr,
                              &vulkanContext->renderFinishedSemaphores.data[i]) != VK_SUCCESS ||
            vkCreateFence(vulkanContext->device, &fenceInfo, nullptr,
                          &vulkanContext->inFlightFences.data[i]) != VK_SUCCESS)
        {
            exitWithError("failed to create synchronization objects for a frame!");
        }
    }
}

internal void
createCommandBuffers(Context* context)
{
    VulkanContext* vulkanContext = context->vulkanContext;

    vulkanContext->commandBuffers = BufferAlloc<VkCommandBuffer>(
        vulkanContext->arena, vulkanContext->swapChainFramebuffers.size);
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = vulkanContext->commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = (uint32_t)vulkanContext->commandBuffers.size;

    if (vkAllocateCommandBuffers(vulkanContext->device, &allocInfo,
                                 vulkanContext->commandBuffers.data) != VK_SUCCESS)
    {
        exitWithError("failed to allocate command buffers!");
    }

#ifdef PROFILING_ENABLE
    context->profilingContext->tracyContexts =
        BufferAlloc<TracyVkCtx>(vulkanContext->arena, vulkanContext->swapChainFramebuffers.size);
    for (U32 i = 0; i < vulkanContext->commandBuffers.size; i++)
    {
        context->profilingContext->tracyContexts.data[i] =
            TracyVkContext(vulkanContext->physicalDevice, vulkanContext->device,
                           vulkanContext->graphicsQueue, vulkanContext->commandBuffers.data[i]);
    }
#endif
}

internal void
createCommandPool(VulkanContext* vulkanContext)
{
    QueueFamilyIndices queueFamilyIndices = vulkanContext->queueFamilyIndices;

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamilyIndex;

    if (vkCreateCommandPool(vulkanContext->device, &poolInfo, nullptr,
                            &vulkanContext->commandPool) != VK_SUCCESS)
    {
        exitWithError("failed to create command pool!");
    }
}

internal void
SwapChainImageViewsCreate(VulkanContext* vulkanContext)
{
    for (uint32_t i = 0; i < vulkanContext->swapChainImages.size; i++)
    {
        vulkanContext->swapChainImageViews.data[i] =
            createImageView(vulkanContext->device, vulkanContext->swapChainImages.data[i],
                            vulkanContext->swapChainImageFormat);
    }
}

internal void
SwapChainImagesCreate(VulkanContext* vulkanContext, SwapChainInfo swapChainInfo, U32 imageCount)
{
    vkGetSwapchainImagesKHR(vulkanContext->device, vulkanContext->swapChain, &imageCount,
                            vulkanContext->swapChainImages.data);

    vulkanContext->swapChainImageFormat = swapChainInfo.surfaceFormat.format;
    vulkanContext->swapChainExtent = swapChainInfo.extent;
}

internal U32
SwapChainImageCountGet(VulkanContext* vulkanContext)
{
    U32 imageCount = {0};
    vkGetSwapchainImagesKHR(vulkanContext->device, vulkanContext->swapChain, &imageCount, nullptr);
    return imageCount;
}

internal SwapChainInfo
SwapChainCreate(Arena* arena, VulkanContext* vulkanContext)
{
    SwapChainInfo swapChainInfo = {0};

    swapChainInfo.swapChainSupport =
        querySwapChainSupport(arena, vulkanContext, vulkanContext->physicalDevice);

    swapChainInfo.surfaceFormat = chooseSwapSurfaceFormat(swapChainInfo.swapChainSupport.formats);
    swapChainInfo.presentMode = chooseSwapPresentMode(swapChainInfo.swapChainSupport.presentModes);
    swapChainInfo.extent =
        chooseSwapExtent(vulkanContext, swapChainInfo.swapChainSupport.capabilities);

    U32 imageCount = swapChainInfo.swapChainSupport.capabilities.minImageCount + 1;

    if (swapChainInfo.swapChainSupport.capabilities.maxImageCount > 0 &&
        imageCount > swapChainInfo.swapChainSupport.capabilities.maxImageCount)
    {
        imageCount = swapChainInfo.swapChainSupport.capabilities.maxImageCount;
    }

    QueueFamilyIndices queueFamilyIndices = vulkanContext->queueFamilyIndices;
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
        U32 queueFamilyIndicesSame[] = {vulkanContext->queueFamilyIndices.graphicsFamilyIndex,
                                        vulkanContext->queueFamilyIndices.presentFamilyIndex};
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
                             &vulkanContext->swapChain) != VK_SUCCESS)
    {
        exitWithError("failed to create swap chain!");
    }

    return swapChainInfo;
}

internal void
createSurface(VulkanContext* vulkanContext)
{
    if (glfwCreateWindowSurface(vulkanContext->instance, vulkanContext->window, nullptr,
                                &vulkanContext->surface) != VK_SUCCESS)
    {
        exitWithError("failed to create window surface!");
    }
}

internal char**
StrArrFromStr8Buffer(Arena* arena, String8* buffer, U64 count)
{
    char** arr = push_array(arena, char*, count);

    for (U32 i = 0; i < count; i++)
    {
        arr[i] = (char*)buffer[i].str;
    }
    return arr;
}

internal void
createInstance(VulkanContext* vulkanContext)
{
    Temp scratch = scratch_begin(0, 0);
    if (vulkanContext->enableValidationLayers && !checkValidationLayerSupport(vulkanContext))
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

    Buffer<String8> extensions = getRequiredExtensions(vulkanContext);

    createInfo.enabledExtensionCount = (U32)extensions.size;
    createInfo.ppEnabledExtensionNames =
        StrArrFromStr8Buffer(scratch.arena, extensions.data, extensions.size);

    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    if (vulkanContext->enableValidationLayers)
    {
        createInfo.enabledLayerCount = (U32)ArrayCount(vulkanContext->validationLayers);
        createInfo.ppEnabledLayerNames = vulkanContext->validationLayers;

        populateDebugMessengerCreateInfo(debugCreateInfo);
        createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT*)&debugCreateInfo;
    }
    else
    {
        createInfo.enabledLayerCount = 0;
        createInfo.pNext = nullptr;
    }

    if (vkCreateInstance(&createInfo, nullptr, &vulkanContext->instance) != VK_SUCCESS)
    {
        exitWithError("failed to create instance!");
    }

    scratch_end(scratch);
}

internal void
createLogicalDevice(Arena* arena, VulkanContext* vulkanContext)
{
    QueueFamilyIndices queueFamilyIndicies = vulkanContext->queueFamilyIndices;

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

    createInfo.enabledExtensionCount = (U32)ArrayCount(vulkanContext->deviceExtensions);
    createInfo.ppEnabledExtensionNames = vulkanContext->deviceExtensions;

    // NOTE: This if statement is no longer necessary on newer versions
    if (vulkanContext->enableValidationLayers)
    {
        createInfo.enabledLayerCount = (U32)ArrayCount(vulkanContext->validationLayers);
        createInfo.ppEnabledLayerNames = vulkanContext->validationLayers;
    }
    else
    {
        createInfo.enabledLayerCount = 0;
    }

    if (vkCreateDevice(vulkanContext->physicalDevice, &createInfo, nullptr,
                       &vulkanContext->device) != VK_SUCCESS)
    {
        exitWithError("failed to create logical device!");
    }

    vkGetDeviceQueue(vulkanContext->device, queueFamilyIndicies.graphicsFamilyIndex, 0,
                     &vulkanContext->graphicsQueue);
    vkGetDeviceQueue(vulkanContext->device, queueFamilyIndicies.presentFamilyIndex, 0,
                     &vulkanContext->presentQueue);
}

internal void
pickPhysicalDevice(VulkanContext* vulkanContext)
{
    Temp scratch = scratch_begin(0, 0);

    vulkanContext->physicalDevice = VK_NULL_HANDLE;

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
        if (isDeviceSuitable(vulkanContext, devices[i], familyIndexBits))
        {
            vulkanContext->physicalDevice = devices[i];
            vulkanContext->msaaSamples = getMaxUsableSampleCount(devices[i]);
            vulkanContext->queueFamilyIndices = QueueFamilyIndicesFromBitFields(familyIndexBits);
            break;
        }
    }

    if (vulkanContext->physicalDevice == VK_NULL_HANDLE)
    {
        exitWithError("failed to find a suitable GPU!");
    }

    scratch_end(scratch);
}

internal bool
isDeviceSuitable(VulkanContext* vulkanContext, VkPhysicalDevice device,
                 QueueFamilyIndexBits indexBits)
{
    // NOTE: This is where you would implement your own checks to see if the
    // device is suitable for your needs

    bool extensionsSupported = checkDeviceExtensionSupport(vulkanContext, device);

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
checkValidationLayerSupport(VulkanContext* vulkanContext)
{
    Temp scratch = scratch_begin(0, 0);
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

    VkLayerProperties* availableLayers = push_array(scratch.arena, VkLayerProperties, layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers);

    bool layerFound = false;
    for (const char* layerName : vulkanContext->validationLayers)
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
getRequiredExtensions(VulkanContext* vulkanContext)
{
    U32 glfwExtensionCount = 0;
    const char** glfwExtensions;
    glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    U32 extensionCount = glfwExtensionCount;
    if (vulkanContext->enableValidationLayers)
    {
        extensionCount++;
    }

    Buffer<String8> extensions = BufferAlloc<String8>(vulkanContext->arena, extensionCount);

    for (U32 i = 0; i < glfwExtensionCount; i++)
    {
        extensions.data[i] = push_str8f(vulkanContext->arena, glfwExtensions[i]);
    }

    if (vulkanContext->enableValidationLayers)
    {
        extensions.data[glfwExtensionCount] =
            push_str8f(vulkanContext->arena, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    return extensions;
}

internal VKAPI_ATTR VkBool32 VKAPI_CALL
debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
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
setupDebugMessenger(VulkanContext* vulkanContext)
{
    if (!vulkanContext->enableValidationLayers)
        return;
    VkDebugUtilsMessengerCreateInfoEXT createInfo;
    populateDebugMessengerCreateInfo(createInfo);
    if (CreateDebugUtilsMessengerEXT(vulkanContext->instance, &createInfo, nullptr,
                                     &vulkanContext->debugMessenger) != VK_SUCCESS)
    {
        exitWithError("failed to set up debug messenger!");
    }
}

internal void
populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo)
{
    createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = debugCallback;
}

internal bool
checkDeviceExtensionSupport(VulkanContext* vulkanContext, VkPhysicalDevice device)
{
    Temp scratch = scratch_begin(0, 0);
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

    VkExtensionProperties* availableExtensions =
        push_array(scratch.arena, VkExtensionProperties, extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions);
    const U64 numberOfRequiredExtenstions = ArrayCount(vulkanContext->deviceExtensions);
    U64 numberOfRequiredExtenstionsLeft = numberOfRequiredExtenstions;
    for (U32 i = 0; i < extensionCount; i++)
    {
        for (U32 j = 0; j < numberOfRequiredExtenstions; j++)
        {
            if (CStrEqual(vulkanContext->deviceExtensions[j], availableExtensions[i].extensionName))
            {
                numberOfRequiredExtenstionsLeft--;
                break;
            }
        }
    }

    return numberOfRequiredExtenstionsLeft == 0;
}

internal VkSurfaceFormatKHR
chooseSwapSurfaceFormat(Buffer<VkSurfaceFormatKHR> availableFormats)
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
chooseSwapPresentMode(Buffer<VkPresentModeKHR> availablePresentModes)
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
chooseSwapExtent(VulkanContext* vulkanContext, const VkSurfaceCapabilitiesKHR& capabilities)
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
getMaxUsableSampleCount(VkPhysicalDevice device)
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
CommandBufferRecord(U32 imageIndex, U32 currentFrame)
{
    ZoneScoped;
    Context* context = GlobalContextGet();

    VulkanContext* vulkanContext = context->vulkanContext;
    ProfilingContext* profilingContext = context->profilingContext;
    (void)profilingContext;

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = 0;                  // Optional
    beginInfo.pInheritanceInfo = nullptr; // Optional

    if (vkBeginCommandBuffer(vulkanContext->commandBuffers.data[currentFrame], &beginInfo) !=
        VK_SUCCESS)
    {
        exitWithError("failed to begin recording command buffer!");
    }

    TracyVkCollect(profilingContext->tracyContexts.data[currentFrame],
                   vulkanContext->commandBuffers.data[currentFrame]);

    // TODO: renderpass here

    if (vkEndCommandBuffer(vulkanContext->commandBuffers.data[currentFrame]) != VK_SUCCESS)
    {
        exitWithError("failed to record command buffer!");
    }
}

C_LINKAGE void
drawFrame()
{
    ZoneScoped;
    Context* context = GlobalContextGet();
    VulkanContext* vulkanContext = context->vulkanContext;

    {
        ZoneScopedN("Wait for frame");
        vkWaitForFences(vulkanContext->device, 1,
                        &vulkanContext->inFlightFences.data[vulkanContext->currentFrame], VK_TRUE,
                        UINT64_MAX);
    }

    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(
        vulkanContext->device, vulkanContext->swapChain, UINT64_MAX,
        vulkanContext->imageAvailableSemaphores.data[vulkanContext->currentFrame], VK_NULL_HANDLE,
        &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
    {
        recreateSwapChain(vulkanContext);
        return;
    }
    else if (result != VK_SUCCESS)
    {
        exitWithError("failed to acquire swap chain image!");
    }

    vkResetFences(vulkanContext->device, 1,
                  &vulkanContext->inFlightFences.data[vulkanContext->currentFrame]);
    vkResetCommandBuffer(vulkanContext->commandBuffers.data[vulkanContext->currentFrame], 0);

    CommandBufferRecord(imageIndex, vulkanContext->currentFrame);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    VkSemaphore waitSemaphores[] = {
        vulkanContext->imageAvailableSemaphores.data[vulkanContext->currentFrame]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &vulkanContext->commandBuffers.data[vulkanContext->currentFrame];

    VkSemaphore signalSemaphores[] = {
        vulkanContext->renderFinishedSemaphores.data[vulkanContext->currentFrame]};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    if (vkQueueSubmit(vulkanContext->graphicsQueue, 1, &submitInfo,
                      vulkanContext->inFlightFences.data[vulkanContext->currentFrame]) !=
        VK_SUCCESS)
    {
        exitWithError("failed to submit draw command buffer!");
    }

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;

    VkSwapchainKHR swapChains[] = {vulkanContext->swapChain};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &imageIndex;

    presentInfo.pResults = nullptr; // Optional

    result = vkQueuePresentKHR(vulkanContext->presentQueue, &presentInfo);
    // TracyVkCollect(tracyContexts[currentFrame], commandBuffers[currentFrame]);
    FrameMark; // end of frame is assumed to be here
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR ||
        vulkanContext->framebufferResized)
    {
        vulkanContext->framebufferResized = 0;
        recreateSwapChain(vulkanContext);
    }
    else if (result != VK_SUCCESS)
    {
        exitWithError("failed to present swap chain image!");
    }

    vulkanContext->currentFrame =
        (vulkanContext->currentFrame + 1) % vulkanContext->MAX_FRAMES_IN_FLIGHT;
}

internal void
recreateSwapChain(VulkanContext* vulkanContext)
{
    Temp scratch = scratch_begin(0, 0);
    int width = 0, height = 0;
    glfwGetFramebufferSize(vulkanContext->window, &width, &height);
    while (width == 0 || height == 0)
    {
        glfwGetFramebufferSize(vulkanContext->window, &width, &height);
        glfwWaitEvents();
    }
    vkDeviceWaitIdle(vulkanContext->device);

    cleanupSwapChain(vulkanContext);

    SwapChainInfo swapChainInfo = SwapChainCreate(scratch.arena, vulkanContext);
    U32 swapChainImageCount = SwapChainImageCountGet(vulkanContext);
    SwapChainImagesCreate(vulkanContext, swapChainInfo, swapChainImageCount);

    SwapChainImageViewsCreate(vulkanContext);
    vulkanContext->colorImageView = createColorResources(
        vulkanContext->physicalDevice, vulkanContext->device, vulkanContext->swapChainImageFormat,
        vulkanContext->swapChainExtent, vulkanContext->msaaSamples, vulkanContext->colorImage,
        vulkanContext->colorImageMemory);

    // TODO: recreate framebuffer
    scratch_end(scratch);
}
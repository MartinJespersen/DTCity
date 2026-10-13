// Exercise the production acquire/present barriers with two frames in flight.
// Run under Xvfb with lavapipe when hardware presentation is unavailable.
#include "test_swapchain_sync.hpp"
#include "render/vulkan/synchronization.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>

SwapchainSyncTest::~SwapchainSyncTest()
{
    if (this->device)
    {
        vkDeviceWaitIdle(this->device);
        for (VkImageView view : this->views) vkDestroyImageView(this->device, view, nullptr);
        for (VkSemaphore semaphore : this->rendered) vkDestroySemaphore(this->device, semaphore, nullptr);
        for (unsigned i = 0; i < 2; ++i)
        {
            vkDestroySemaphore(this->device, this->acquired[i], nullptr);
            vkDestroyFence(this->device, this->fences[i], nullptr);
        }
        vkDestroyCommandPool(this->device, this->pool, nullptr);
        vkDestroySwapchainKHR(this->device, this->swapchain, nullptr);
        vkDestroyDevice(this->device, nullptr);
    }
    if (this->surface) vkDestroySurfaceKHR(this->instance, this->surface, nullptr);
    if (this->messenger)
    {
        auto destroy = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(this->instance, "vkDestroyDebugUtilsMessengerEXT");
        destroy(this->instance, this->messenger, nullptr);
    }
    if (this->instance) vkDestroyInstance(this->instance, nullptr);
    if (this->window) glfwDestroyWindow(this->window);
    glfwTerminate();
}

int main(int argc, char** argv)
{
    SwapchainSyncTest test = {};
    bool legacy_scopes = argc == 2 && std::strcmp(argv[1], "--legacy-scopes") == 0;
    if (!glfwInit()) return 77;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    test.window = glfwCreateWindow(64, 64, "Swapchain synchronization test", nullptr, nullptr);
    if (!test.window) return 77;
    uint32_t layer_count = 0;
    _swapchain_test_check(vkEnumerateInstanceLayerProperties(&layer_count, nullptr));
    std::vector<VkLayerProperties> layers(layer_count);
    _swapchain_test_check(vkEnumerateInstanceLayerProperties(&layer_count, layers.data()));
    bool validation = false;
    for (VkLayerProperties& layer : layers)
        validation |= std::strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0;
    if (!validation) return 77;
    uint32_t extension_count = 0;
    const char** required = glfwGetRequiredInstanceExtensions(&extension_count);
    if (!required) return 77;
    std::vector<const char*> extensions(required, required + extension_count);
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    const char* layer_name = "VK_LAYER_KHRONOS_validation";
    VkValidationFeatureEnableEXT enable = VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
    VkValidationFeaturesEXT validation_info{VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT};
    validation_info.enabledValidationFeatureCount = 1;
    validation_info.pEnabledValidationFeatures = &enable;
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo instance_info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instance_info.pNext = &validation_info;
    instance_info.pApplicationInfo = &app;
    instance_info.enabledLayerCount = 1;
    instance_info.ppEnabledLayerNames = &layer_name;
    instance_info.enabledExtensionCount = (uint32_t)extensions.size();
    instance_info.ppEnabledExtensionNames = extensions.data();
    _swapchain_test_check(vkCreateInstance(&instance_info, nullptr, &test.instance));
    VkDebugUtilsMessengerCreateInfoEXT messenger_info{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    messenger_info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    messenger_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
    messenger_info.pfnUserCallback = _swapchain_test_validation;
    messenger_info.pUserData = &test;
    auto create_messenger = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(test.instance, "vkCreateDebugUtilsMessengerEXT");
    _swapchain_test_check(create_messenger(test.instance, &messenger_info, nullptr, &test.messenger));
    _swapchain_test_check(glfwCreateWindowSurface(test.instance, test.window, nullptr, &test.surface));

    uint32_t device_count = 0;
    _swapchain_test_check(vkEnumeratePhysicalDevices(test.instance, &device_count, nullptr));
    std::vector<VkPhysicalDevice> devices(device_count);
    _swapchain_test_check(vkEnumeratePhysicalDevices(test.instance, &device_count, devices.data()));
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    uint32_t family = 0;
    for (VkPhysicalDevice candidate : devices)
    {
        VkPhysicalDeviceVulkan13Features features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        VkPhysicalDeviceFeatures2 query{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        query.pNext = &features;
        vkGetPhysicalDeviceFeatures2(candidate, &query);
        if (!features.dynamicRendering || !features.synchronization2) continue;
        uint32_t count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &count, nullptr);
        std::vector<VkQueueFamilyProperties> families(count);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &count, families.data());
        for (uint32_t i = 0; i < count; ++i)
        {
            VkBool32 present = VK_FALSE;
            _swapchain_test_check(vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, test.surface, &present));
            if (present && (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT))
            {
                physical_device = candidate;
                family = i;
                break;
            }
        }
        if (physical_device) break;
    }
    if (!physical_device) return 77;
    float priority = 1;
    VkDeviceQueueCreateInfo queue_info{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queue_info.queueFamilyIndex = family;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &priority;
    VkPhysicalDeviceVulkan13Features features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    features.dynamicRendering = VK_TRUE;
    features.synchronization2 = VK_TRUE;
    const char* swapchain_extension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    VkDeviceCreateInfo device_info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    device_info.pNext = &features;
    device_info.queueCreateInfoCount = 1;
    device_info.pQueueCreateInfos = &queue_info;
    device_info.enabledExtensionCount = 1;
    device_info.ppEnabledExtensionNames = &swapchain_extension;
    _swapchain_test_check(vkCreateDevice(physical_device, &device_info, nullptr, &test.device));
    vkGetDeviceQueue(test.device, family, 0, &test.queue);
    VkSurfaceCapabilitiesKHR capabilities{};
    _swapchain_test_check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, test.surface, &capabilities));
    uint32_t format_count = 0;
    _swapchain_test_check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, test.surface, &format_count, nullptr));
    std::vector<VkSurfaceFormatKHR> formats(format_count);
    _swapchain_test_check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, test.surface, &format_count, formats.data()));
    VkSwapchainCreateInfoKHR swapchain_info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    swapchain_info.surface = test.surface;
    swapchain_info.minImageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount) swapchain_info.minImageCount = std::min(swapchain_info.minImageCount, capabilities.maxImageCount);
    swapchain_info.imageFormat = formats[0].format;
    swapchain_info.imageColorSpace = formats[0].colorSpace;
    swapchain_info.imageExtent = capabilities.currentExtent.width == UINT32_MAX ? VkExtent2D{64, 64} : capabilities.currentExtent;
    swapchain_info.imageArrayLayers = 1;
    swapchain_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapchain_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchain_info.preTransform = capabilities.currentTransform;
    swapchain_info.compositeAlpha = (VkCompositeAlphaFlagBitsKHR)(capabilities.supportedCompositeAlpha & -capabilities.supportedCompositeAlpha);
    swapchain_info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    _swapchain_test_check(vkCreateSwapchainKHR(test.device, &swapchain_info, nullptr, &test.swapchain));
    uint32_t image_count = 0;
    _swapchain_test_check(vkGetSwapchainImagesKHR(test.device, test.swapchain, &image_count, nullptr));
    std::vector<VkImage> images(image_count);
    _swapchain_test_check(vkGetSwapchainImagesKHR(test.device, test.swapchain, &image_count, images.data()));
    test.views.resize(image_count);
    test.rendered.resize(image_count);
    VkSemaphoreCreateInfo semaphore_info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for (uint32_t i = 0; i < image_count; ++i)
    {
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image = images[i];
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = formats[0].format;
        view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        _swapchain_test_check(vkCreateImageView(test.device, &view, nullptr, &test.views[i]));
        _swapchain_test_check(vkCreateSemaphore(test.device, &semaphore_info, nullptr, &test.rendered[i]));
    }
    VkCommandPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool_info.queueFamilyIndex = family;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    _swapchain_test_check(vkCreateCommandPool(test.device, &pool_info, nullptr, &test.pool));
    VkCommandBuffer cmds[2];
    VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocate.commandPool = test.pool;
    allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate.commandBufferCount = 2;
    _swapchain_test_check(vkAllocateCommandBuffers(test.device, &allocate, cmds));
    VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (uint32_t i = 0; i < 2; ++i)
    {
        _swapchain_test_check(vkCreateSemaphore(test.device, &semaphore_info, nullptr, &test.acquired[i]));
        _swapchain_test_check(vkCreateFence(test.device, &fence_info, nullptr, &test.fences[i]));
    }
    for (uint32_t iteration = 0; iteration < 128; ++iteration)
    {
        uint32_t frame = iteration % 2;
        _swapchain_test_check(vkWaitForFences(test.device, 1, &test.fences[frame], VK_TRUE, UINT64_MAX));
        _swapchain_test_check(vkResetFences(test.device, 1, &test.fences[frame]));
        uint32_t image = 0;
        _swapchain_test_check(vkAcquireNextImageKHR(test.device, test.swapchain, UINT64_MAX, test.acquired[frame], VK_NULL_HANDLE, &image));
        _swapchain_test_check(vkResetCommandBuffer(cmds[frame], 0));
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        _swapchain_test_check(vkBeginCommandBuffer(cmds[frame], &begin));
        VkImageMemoryBarrier2 barrier = vulkan::vulkan_swapchain_image_barrier(images[image], false);
        if (legacy_scopes) barrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
        VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dependency.imageMemoryBarrierCount = 1;
        dependency.pImageMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(cmds[frame], &dependency);
        VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        color.imageView = test.views[image];
        color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.clearValue.color.float32[0] = float(iteration % 2);
        VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
        rendering.renderArea.extent = swapchain_info.imageExtent;
        rendering.layerCount = 1;
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachments = &color;
        vkCmdBeginRendering(cmds[frame], &rendering);
        vkCmdEndRendering(cmds[frame]);
        barrier = vulkan::vulkan_swapchain_image_barrier(images[image], true);
        if (legacy_scopes) barrier.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
        vkCmdPipelineBarrier2(cmds[frame], &dependency);
        _swapchain_test_check(vkEndCommandBuffer(cmds[frame]));
        VkSemaphoreSubmitInfo wait{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
        wait.semaphore = test.acquired[frame];
        wait.stageMask = legacy_scopes ? VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT : VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSemaphoreSubmitInfo signal{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
        signal.semaphore = test.rendered[image];
        signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        VkCommandBufferSubmitInfo command{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
        command.commandBuffer = cmds[frame];
        VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
        submit.waitSemaphoreInfoCount = 1;
        submit.pWaitSemaphoreInfos = &wait;
        submit.signalSemaphoreInfoCount = 1;
        submit.pSignalSemaphoreInfos = &signal;
        submit.commandBufferInfoCount = 1;
        submit.pCommandBufferInfos = &command;
        _swapchain_test_check(vkQueueSubmit2(test.queue, 1, &submit, test.fences[frame]));
        VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &test.rendered[image];
        present.swapchainCount = 1;
        present.pSwapchains = &test.swapchain;
        present.pImageIndices = &image;
        _swapchain_test_check(vkQueuePresentKHR(test.queue, &present));
        glfwPollEvents();
        if (test.errors) return 1;
    }
    _swapchain_test_check(vkDeviceWaitIdle(test.device));
    std::puts("PASS: 128 acquire/render/present cycles with synchronization validation");
    return test.errors ? 1 : 0;
}

static void
_swapchain_test_check(VkResult result)
{
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
    {
        std::fprintf(stderr, "Vulkan error: %d\n", result);
        std::exit(1);
    }
}

static VKAPI_ATTR VkBool32 VKAPI_CALL
_swapchain_test_validation(VkDebugUtilsMessageSeverityFlagBitsEXT, VkDebugUtilsMessageTypeFlagsEXT,
                           const VkDebugUtilsMessengerCallbackDataEXT* data, void* user)
{
    ++((SwapchainSyncTest*)user)->errors;
    std::fprintf(stderr, "%s\n", data->pMessage);
    return VK_FALSE;
}

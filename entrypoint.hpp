#pragma once

// user defined: [hpp]
#include "base/base_inc.hpp"
#include "ui/ui.hpp"

const U32 MAX_FONTS_IN_USE = 10;

extern "C"
{
    void
    InitContext();
    void
    DeleteContext();
    void
    drawFrame();
}

internal void
cleanup();

internal void
VulkanInit();

internal void
initWindow();

internal void
framebufferResizeCallback(GLFWwindow* window, int width, int height);

internal void
CommandBufferRecord(U32 imageIndex, U32 currentFrame);

internal void
recreateSwapChain(VulkanContext* vulkanContext);

internal void
createSyncObjects(VulkanContext* vulkanContext);

internal void
createCommandBuffers(Context* context);

internal void
SwapChainImageViewsCreate(VulkanContext* vulkanContext);
internal void
createCommandPool(VulkanContext* vulkanContext);

internal void
cleanupColorResources(VulkanContext* vulkanContext);
internal void
cleanupSwapChain(VulkanContext* vulkanContext);
internal void
createInstance(VulkanContext* vulkanContext);
internal void
setupDebugMessenger(VulkanContext* vulkanContext);
internal void
createSurface(VulkanContext* vulkanContext);
internal void
pickPhysicalDevice(VulkanContext* vulkanContext);
internal void
createLogicalDevice(Arena* arena, VulkanContext* vulkanContext);

internal SwapChainInfo
SwapChainCreate(Arena* arena, VulkanContext* vulkanContext);
internal U32
SwapChainImageCountGet(VulkanContext* vulkanContext);
internal void
SwapChainImagesCreate(VulkanContext* vulkanContext, SwapChainInfo swapChainInfo, U32 imageCount);

internal VkExtent2D
chooseSwapExtent(VulkanContext* vulkanContext, const VkSurfaceCapabilitiesKHR& capabilities);

internal Buffer<String8>
getRequiredExtensions(VulkanContext* vulkanContext);

internal void
populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo);

internal bool
isDeviceSuitable(VulkanContext* vulkanContext, VkPhysicalDevice device,
                 QueueFamilyIndexBits indexBits);

internal VkSampleCountFlagBits
getMaxUsableSampleCount(VkPhysicalDevice device);

internal bool
checkDeviceExtensionSupport(VulkanContext* vulkanContext, VkPhysicalDevice device);

internal bool
checkValidationLayerSupport(VulkanContext* vulkanContext);

internal VkSurfaceFormatKHR
chooseSwapSurfaceFormat(Buffer<VkSurfaceFormatKHR> availableFormats);

internal VkPresentModeKHR
chooseSwapPresentMode(Buffer<VkPresentModeKHR> availablePresentModes);

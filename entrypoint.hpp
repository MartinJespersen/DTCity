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
VK_Cleanup();

internal void
VulkanInit();

internal void
InitWindow();

internal void
framebufferResizeCallback(GLFWwindow* window, int width, int height);

internal void
CommandBufferRecord(U32 imageIndex, U32 currentFrame);

internal void
recreateSwapChain(VulkanContext* vulkanContext);

internal void
VK_SyncObjectsCreate(VulkanContext* vulkanContext);

internal void
VK_CommandBuffersCreate(Context* context);

internal void
VK_SwapChainImageViewsCreate(VulkanContext* vulkanContext);
internal void
VK_CommandPoolCreate(VulkanContext* vulkanContext);

internal void
VK_ColorResourcesCleanup(VulkanContext* vulkanContext);
internal void
VK_SwapChainCleanup(VulkanContext* vulkanContext);
internal void
createInstance(VulkanContext* vulkanContext);
internal void
setupDebugMessenger(VulkanContext* vulkanContext);
internal void
VK_SurfaceCreate(VulkanContext* vulkanContext);
internal void
pickPhysicalDevice(VulkanContext* vulkanContext);
internal void
createLogicalDevice(Arena* arena, VulkanContext* vulkanContext);

internal SwapChainInfo
VK_SwapChainCreate(Arena* arena, VulkanContext* vulkanContext);
internal U32
VK_SwapChainImageCountGet(VulkanContext* vulkanContext);
internal void
VK_SwapChainImagesCreate(VulkanContext* vulkanContext, SwapChainInfo swapChainInfo, U32 imageCount);

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

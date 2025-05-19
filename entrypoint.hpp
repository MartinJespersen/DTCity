#pragma once

// user defined: [hpp]
#include "base/base.hpp"
#include "ui/ui.hpp"

const u64 FONT_ARENA_SIZE = MEGABYTE(4);
const u32 MAX_FONTS_IN_USE = 10;

extern "C"
{
    void
    InitContext();
    void
    DeleteContext();
    void
    drawFrame();
}

root_function void
cleanup();

root_function void
VulkanInit();

root_function void
initWindow();

root_function void
framebufferResizeCallback(GLFWwindow* window, int width, int height);

root_function void
CommandBufferRecord(u32 imageIndex, u32 currentFrame);

root_function void
recreateSwapChain(VulkanContext* vulkanContext);

root_function void
createSyncObjects(VulkanContext* vulkanContext);

root_function void
createCommandBuffers(Context* context);

root_function void
SwapChainImageViewsCreate(VulkanContext* vulkanContext);
root_function void
createCommandPool(VulkanContext* vulkanContext);

root_function void
cleanupColorResources(VulkanContext* vulkanContext);
root_function void
cleanupSwapChain(VulkanContext* vulkanContext);
root_function void
createInstance(VulkanContext* vulkanContext);
root_function void
setupDebugMessenger(VulkanContext* vulkanContext);
root_function void
createSurface(VulkanContext* vulkanContext);
root_function void
pickPhysicalDevice(VulkanContext* vulkanContext);
root_function void
createLogicalDevice(Arena* arena, VulkanContext* vulkanContext);

root_function SwapChainInfo
SwapChainCreate(Arena* arena, VulkanContext* vulkanContext);
root_function u32
SwapChainImageCountGet(VulkanContext* vulkanContext);
root_function void
SwapChainImagesCreate(VulkanContext* vulkanContext, SwapChainInfo swapChainInfo, u32 imageCount);

root_function VkExtent2D
chooseSwapExtent(VulkanContext* vulkanContext, const VkSurfaceCapabilitiesKHR& capabilities);

root_function String8_Buffer
getRequiredExtensions(VulkanContext* vulkanContext);

root_function void
populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo);

root_function bool
isDeviceSuitable(VulkanContext* vulkanContext, VkPhysicalDevice device,
                 QueueFamilyIndexBits indexBits);

                 root_function VkSampleCountFlagBits
getMaxUsableSampleCount(VkPhysicalDevice device);

root_function bool
checkDeviceExtensionSupport(VulkanContext* vulkanContext, VkPhysicalDevice device);

root_function bool
checkValidationLayerSupport(VulkanContext* vulkanContext);

root_function VkSurfaceFormatKHR
chooseSwapSurfaceFormat(VkSurfaceFormatKHR_Buffer availableFormats);

root_function VkPresentModeKHR
chooseSwapPresentMode(VkPresentModeKHR_Buffer availablePresentModes);

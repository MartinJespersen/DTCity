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
    DrawFrame();
}

internal void
VK_Cleanup();

internal void
VK_VulkanInit();

internal void
InitWindow();

internal void
VK_FramebufferResizeCallback(GLFWwindow* window, int width, int height);

internal void
CommandBufferRecord(U32 imageIndex, U32 currentFrame);

internal void
VK_RecreateSwapChain(VulkanContext* vulkanContext);

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
VK_CreateInstance(VulkanContext* vulkanContext);
internal void
VK_DebugMessengerSetup(VulkanContext* vulkanContext);
internal void
VK_SurfaceCreate(VulkanContext* vulkanContext);
internal void
VK_PhysicalDevicePick(VulkanContext* vulkanContext);
internal void
VK_LogicalDeviceCreate(Arena* arena, VulkanContext* vulkanContext);

internal SwapChainInfo
VK_SwapChainCreate(Arena* arena, VulkanContext* vulkanContext);
internal U32
VK_SwapChainImageCountGet(VulkanContext* vulkanContext);
internal void
VK_SwapChainImagesCreate(VulkanContext* vulkanContext, SwapChainInfo swapChainInfo, U32 imageCount);

internal VkExtent2D
VK_ChooseSwapExtent(VulkanContext* vulkanContext, const VkSurfaceCapabilitiesKHR& capabilities);

internal Buffer<String8>
VK_RequiredExtensionsGet(VulkanContext* vulkanContext);

internal void
VK_PopulateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo);

internal bool
VK_IsDeviceSuitable(VulkanContext* vulkanContext, VkPhysicalDevice device,
                    QueueFamilyIndexBits indexBits);

internal VkSampleCountFlagBits
VK_MaxUsableSampleCountGet(VkPhysicalDevice device);

internal bool
VK_CheckDeviceExtensionSupport(VulkanContext* vulkanContext, VkPhysicalDevice device);

internal bool
VK_CheckValidationLayerSupport(VulkanContext* vulkanContext);

internal VkSurfaceFormatKHR
VK_ChooseSwapSurfaceFormat(Buffer<VkSurfaceFormatKHR> availableFormats);

internal VkPresentModeKHR
VK_ChooseSwapPresentMode(Buffer<VkPresentModeKHR> availablePresentModes);

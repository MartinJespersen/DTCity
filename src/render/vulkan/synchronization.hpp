#pragma once
#include <vulkan/vulkan.h>

namespace vulkan
{
VkImageMemoryBarrier2
vulkan_swapchain_image_barrier(VkImage image, bool present);
void
vulkan_upload_barrier_record(VkCommandBuffer command_buffer);
}

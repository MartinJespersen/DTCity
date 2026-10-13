#pragma once
#include <vulkan/vulkan.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <vector>

struct SwapchainSyncTest
{
    GLFWwindow* window;
    VkInstance instance;
    VkDebugUtilsMessengerEXT messenger;
    VkSurfaceKHR surface;
    VkDevice device;
    VkQueue queue;
    VkSwapchainKHR swapchain;
    VkCommandPool pool;
    VkSemaphore acquired[2];
    VkFence fences[2];
    std::vector<VkImageView> views;
    std::vector<VkSemaphore> rendered;
    unsigned errors;

    ~SwapchainSyncTest();
};

int main(int argc, char** argv);
static void
_swapchain_test_check(VkResult result);
static VKAPI_ATTR VkBool32 VKAPI_CALL
_swapchain_test_validation(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT type,
                           const VkDebugUtilsMessengerCallbackDataEXT* data, void* user);

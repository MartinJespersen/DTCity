// ~mgj: Third party includes
DISABLE_WARNINGS_PUSH
#include <vulkan/vulkan_core.h>
#undef VK_CHECK_RESULT
#define KHRONOS_STATIC
#include "third_party/ktx/include/ktx.h"
#define VMA_VULKAN_VERSION 1003000 // Vulkan 1.3
#define VMA_DEBUG_DETECT_CORRUPTION 1
#define VMA_DEBUG_DETECT_LEAKS 1
#include "third_party/vk_mem_alloc.h"
DISABLE_WARNINGS_POP

#if OS_WINDOWS

#pragma comment(lib, "glfw3.lib")
#pragma comment(lib, "vulkan-1.lib")
#pragma comment(lib, "ktx_read.lib")
#endif

#include "third_party/tracy/tracy/TracyVulkan.hpp"
#include "third_party/tracy/tracy/Tracy.hpp"

// ~mgj: user lib includes
#include "render.hpp"
#include "vulkan/vulkan_common.hpp"
#include "vulkan/vulkan.hpp"

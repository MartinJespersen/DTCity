// ~mgj: Third party includes
DISABLE_WARNINGS_PUSH
#include <vulkan/vulkan_core.h>
#undef VK_CHECK_RESULT
#define KHRONOS_STATIC
#include "ktx.h"
#include "ktxvulkan.h"
#define VMA_VULKAN_VERSION 1003000 // Vulkan 1.3
#define VMA_DEBUG_DETECT_CORRUPTION 1
#define VMA_DEBUG_DETECT_LEAKS 1
#define VMA_ASSERT(x) Assert(x)
#include "third_party/vk_mem_alloc.h"
#include "stb_image.h"

DISABLE_WARNINGS_POP

#include "third_party/tracy/tracy/TracyVulkan.hpp"
#include "third_party/tracy/tracy/Tracy.hpp"

// ~mgj: user lib includes
#include "render.hpp"
#include "vulkan/asset_manager.hpp"
#include "vulkan/vulkan_common.hpp"
#include "vulkan/vulkan.hpp"

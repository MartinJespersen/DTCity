// ~mgj: third party libs
DISABLE_WARNINGS_PUSH
#define VMA_IMPLEMENTATION
#include "third_party/vk_mem_alloc.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include "third_party/stb_image.h"

DISABLE_WARNINGS_POP

// ~mgj: user libs
#include "render.cpp"

#include "vulkan/vulkan_common.cpp"
#include "vulkan/asset_manager.cpp"
#include "vulkan/vulkan.cpp"
#include "vulkan/vulkan_if.cpp"

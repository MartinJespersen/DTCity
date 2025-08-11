#pragma once
// ~mgj: third party libs
#include <vulkan/vulkan_core.h>
#include "third_party/simdjson/simdjson.h"

#define KHRONOS_STATIC
#include "third_party/ktx/include/ktx.h"

#define VMA_VULKAN_VERSION 1003000 // Vulkan 1.3
#define VMA_DEBUG_DETECT_CORRUPTION 1
#define VMA_DEBUG_DETECT_LEAKS 1
#include "third_party/vk_mem_alloc.h"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"

// ~mgj: user defined
#include "lib_wrappers/json.hpp"
#include "lib_wrappers/cgltf.hpp"
#include "lib_wrappers/vulkan_common.hpp"
#include "lib_wrappers/vulkan.hpp"

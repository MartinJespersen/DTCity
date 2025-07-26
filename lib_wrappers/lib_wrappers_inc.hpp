#pragma once
// ~mgj: third party libs
#include <vulkan/vulkan_core.h>
#include "third_party/simdjson/simdjson.h"

#define VMA_VULKAN_VERSION 1003000 // Vulkan 1.3
#include "third_party/vk_mem_alloc.h"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"

// ~mgj: user defined
#include "lib_wrappers/json.hpp"
#include "lib_wrappers/vulkan.hpp"

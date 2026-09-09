#pragma once

///////////////////////////////////////////////////////////////////
// std includes
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

#include "debug_forward_ref.hpp"
// helper diagnostics
#include "diagnostics.hpp"

#include <CDT.h>
#include <meshoptimizer.h>
// cesium native libraries
#include "cesium/cesium_native_headers.hpp"

#undef APIENTRY
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#undef APIENTRY

//////////////////////////////////////////////
// mgj: third party libs
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES
#define GLM_FORCE_INTRINSICS
#define GLM_FORCE_INLINE
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"

//////////////////////////////////////////////
// mgj: imgui
#define IM_ASSERT(x) assert(x)
#pragma push_macro("Swap")
#pragma push_macro("Min")
#pragma push_macro("Max")
#undef Swap
#undef Min
#undef Max
#define IMGUI_DEFINE_MATH_OPERATORS
#include "third_party/imgui/imgui.h"
#include "third_party/imgui/backends/imgui_impl_glfw.h"
#include "third_party/imgui/backends/imgui_impl_vulkan.h"
#pragma pop_macro("Swap")
#pragma pop_macro("Min")
#pragma pop_macro("Max")

#if (BUILD_DEBUG)
#define SIMDJSON_DEVELOPMENT_CHECKS 1
#endif
#include "simdjson/simdjson.h"

#include "curl/curl.h"

//////////////////////////////////////////////
// mgj: Vulkan support libraries
#include <vulkan/vulkan_core.h>
#undef VK_CHECK_RESULT
#ifndef KHRONOS_STATIC
#define KHRONOS_STATIC
#endif
#include "ktx.h"
#include "ktxvulkan.h"

#define VMA_VULKAN_VERSION 1003000 // Vulkan 1.3
#define VMA_DEBUG_DETECT_CORRUPTION 1
#define VMA_DEBUG_DETECT_LEAKS 1
#define VMA_ASSERT(x) ASSERT(x, "VMA assertion failed")
#include "third_party/vk_mem_alloc.h"

#include "stb_image.h"
#include "third_party/tracy/tracy/TracyVulkan.hpp"
#include "third_party/tracy/tracy/Tracy.hpp"

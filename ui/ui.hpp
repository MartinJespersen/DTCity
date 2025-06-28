#pragma once
// third party libs

#define STB_IMAGE_STATIC
#include <stb_image.h>

#include <vulkan/vulkan_core.h>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"

#include <string>
#include <stdio.h>
#include <time.h>

// profiler
#include "profiler/tracy/Tracy.hpp"
#include "profiler/tracy/TracyVulkan.hpp"

// user defined headers
#include "base/base_inc.hpp"
#include "io.hpp"
#include "vulkan_helpers.hpp"
#include "state.hpp"
#include "globals.hpp"
#include "geometry.hpp"
#include "terrain.hpp"

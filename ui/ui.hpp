#pragma once
// third party libs
#include <vulkan/vulkan_core.h>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <string>
#include <stdio.h>
#include <time.h>

// profiler
#include "profiler/tracy/Tracy.hpp"
#include "profiler/tracy/TracyVulkan.hpp"

// user defined headers
#include "base/base_inc.hpp"
#include "vulkan_helpers.hpp"
#include "state.hpp"
#include "globals.hpp"
#include "terrain.hpp"
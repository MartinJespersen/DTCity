#pragma once
// third party libs

#define STB_IMAGE_STATIC
#include "third_party/stb_image.h"

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

// user defined headers
#include "base/base_inc.hpp"
#include "os_core/os_core_inc.hpp"
#include "io.hpp"
#include "state.hpp"
#include "globals.hpp"
#include "geometry.hpp"
#include "terrain.hpp"

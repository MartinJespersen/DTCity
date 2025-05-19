#pragma once

// third party libs
#include <vulkan/vulkan_core.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <string>
#include <stdio.h>
// profiler
#include "profiler/tracy/Tracy.hpp"
#include "profiler/tracy/TracyVulkan.hpp"

// user defined headers
#include "vulkan_helpers.hpp"
#include "box.hpp"
#include "fonts.hpp"
#include "state.hpp"
#include "globals.hpp"
#include "widget.hpp"
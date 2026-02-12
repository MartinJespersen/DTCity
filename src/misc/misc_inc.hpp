#pragma once
/////////////////////////////////////////
// third party libs
#include <stdio.h>
#include <time.h>
#include <atomic>

#undef APIENTRY
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#undef APIENTRY

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
//////////////////////////////////////////////////////
// user defined headers
#include "base/base_inc.hpp"
#include "os_core/os_core_inc.hpp"
#include "io.hpp"
#include "async/async.hpp"
#include "geometry.hpp"
#include "camera.hpp"

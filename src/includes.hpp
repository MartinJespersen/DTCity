#pragma once

///////////////////////////////////////////////////////////////////
// std includes
#include <cstdio>
#include <cstdlib>
#include <cstring>

// helper diagnostics
#include "diagnostics.hpp"

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
#define IM_ASSERT(x) assert(x)
#include "third_party/imgui/imgui_inc.hpp"

//////////////////////////////////////////////

DISABLE_WARNINGS_PUSH
#define OS_FEATURE_GRAPHICAL 1
#include "base/base_inc.hpp"
#include "os_core/os_core_inc.hpp"
DISABLE_WARNINGS_POP

#if (BUILD_DEBUG)
#define SIMDJSON_DEVELOPMENT_CHECKS 1
#endif
#include "simdjson/simdjson.h"

// user defined: [hpp]
#include "utility/utility_inc.hpp"
#include "async/async_inc.hpp"
#include "render/render_inc.hpp"
#include "draw/draw.hpp"
#include "misc/misc_inc.hpp"
#include "lib_wrappers/lib_wrappers_inc.hpp"
#include "gltfw/gltfw.hpp"
#include "osm/osm.hpp"
#include "cesium/cesium_tileset.hpp"
#include "city/city_inc.hpp"
#include "entrypoint.hpp"

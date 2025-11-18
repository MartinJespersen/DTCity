#pragma once

#include <cstdio>
#include <cstdlib>
#include <cstring>

#undef APIENTRY
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#undef APIENTRY

#include "base/base_inc.hpp"
DISABLE_WARNINGS_PUSH
//////////////////////////////////////////////
// mgj: third party libs
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES
#include "third_party/glm/glm.hpp"
#include "third_party/glm/gtc/matrix_transform.hpp"
#include "third_party/imgui/imgui_inc.hpp"
//////////////////////////////////////////////

#include "os_core/os_core_inc.hpp"
DISABLE_WARNINGS_POP

// user defined: [hpp]
#include "async/async.hpp"
#include "http/http_inc.hpp"
#include "misc/misc_inc.hpp"
#include "render/render_inc.hpp"
#include "lib_wrappers/lib_wrappers_inc.hpp"
#include "osm/osm.hpp"
#include "city/city_inc.hpp"
#include "entrypoint.hpp"

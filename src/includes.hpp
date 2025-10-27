#include <cstdio>
#include <cstdlib>
#include <cstring>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan_core.h>

#include "base/base_inc.hpp"

// third party libs
DISABLE_WARNINGS_PUSH
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES
#include "third_party/glm/glm.hpp"
#include "third_party/glm/gtc/matrix_transform.hpp"
DISABLE_WARNINGS_POP

// user defined: [hpp]
#include "os_core/os_core_inc.hpp"
#include "async/async.hpp"
#include "http/http_inc.hpp"
#include "gfx/gfx_inc.hpp"
#include "render/render_inc.hpp"
#include "lib_wrappers/lib_wrappers_inc.hpp"
#include "imgui/imgui_inc.hpp"
#include "osm/osm.hpp"
#include "city/city_inc.hpp"
#include "entrypoint.hpp"

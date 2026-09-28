#pragma once

#include "pch.hpp"


DISABLE_WARNINGS_PUSH
#define OS_FEATURE_GRAPHICAL 1
#include "base/base_inc.hpp"
#include "os_core/os_core_inc.hpp"
DISABLE_WARNINGS_POP
#include "debug_log.hpp"

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

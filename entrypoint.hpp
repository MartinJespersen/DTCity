#pragma once

#include "base/base_inc.hpp"

// third party libs#define GLM_FORCE_RADIANS
DISABLE_WARNINGS_PUSH
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
DISABLE_WARNINGS_POP

// user defined: [hpp]
#include "os_core/os_core_inc.hpp"
#include "async/async.hpp"
#include "http/http_inc.hpp"
#include "ui/ui.hpp"
#include "render/render_inc.hpp"
#include "lib_wrappers/lib_wrappers_inc.hpp"
#include "imgui/imgui_inc.hpp"
#include "city/city_inc.hpp"

struct Context
{
    B32 running;
    String8 cwd;
    String8 cache_path;
    String8 asset_path;
    String8 texture_path;
    String8 shader_path;

    Arena* arena_permanent;

    IO* io;
    ui::Camera* camera;
    DT_Time* time;
    city::Road* road;
    city::CarSim* car_sim;
    city::Buildings* buildings;

    async::Threads* thread_pool;
    // TODO: change this. moves os global state for use in DLL
    void* os_state;
};

const U32 MAX_FONTS_IN_USE = 10;

static OS_Handle
Entrypoint(void* ptr);

static void
Cleanup(OS_Handle thread_handle, void* ptr);

static void
MainLoop(void* ptr);

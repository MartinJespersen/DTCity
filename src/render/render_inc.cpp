#include "core_inc.hpp"
#include "base/base_container.hpp"
#include "base/base_container_templates.hpp"
#include "base/base_lists.hpp"
#include "base/base_lists_templates.hpp"
#include "async/thread_pool.hpp"
#include "render/render_inc.hpp"
#include "draw/draw.hpp"
#include "misc/io.hpp"
#include "entrypoint.hpp"

// ~mgj: user libs
#include "render.cpp"

#include "vulkan/vulkan_common.cpp"
#include "vulkan/asset_manager.cpp"
#include "vulkan/vulkan.cpp"
#include "vulkan/vulkan_if.cpp"
#include "vulkan/pipelines.cpp"

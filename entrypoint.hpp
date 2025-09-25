#pragma once

// user defined: [hpp]
#include "base/base_inc.hpp"
#include "os_core/os_core_inc.hpp"
#include "async/async.hpp"
#include "http/http_inc.hpp"
#include "draw/draw_inc.hpp"
#include "font_provider/font_provider_inc.hpp"
#include "render/render_inc.hpp"
#include "font_cache/font_cache.hpp"
#include "lib_wrappers/lib_wrappers_inc.hpp"
#include "city/city_inc.hpp"
#include "ui/ui.hpp"

const U32 MAX_FONTS_IN_USE = 10;

static OS_Handle
Entrypoint(void* ptr);

static void
Cleanup(OS_Handle thread_handle, void* ptr);

static void
MainLoop(void* ptr);
static void
CommandBufferRecord(U32 imageIndex, U32 currentFrame);
static void
ProfileBuffersCreate(wrapper::VulkanContext* vk_ctx);
static void
ProfileBuffersDestroy(wrapper::VulkanContext* vk_ctx);

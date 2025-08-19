#pragma once

// user defined: [hpp]
#include "base/base_inc.hpp"
#include "os_core/os_core_inc.hpp"
#include "async/async.hpp"
#include "http/http_inc.hpp"
#include "lib_wrappers/lib_wrappers_inc.hpp"
#include "ui/ui.hpp"
#include "city/city_inc.hpp"

const U32 MAX_FONTS_IN_USE = 10;

shared_function OS_Handle
Entrypoint(void* ptr);

shared_function void
Cleanup(void* ptr);

static void
MainLoop(void* ptr);
static void
CommandBufferRecord(U32 imageIndex, U32 currentFrame);
static void
ProfileBuffersCreate(wrapper::VulkanContext* vk_ctx);
static void
ProfileBuffersDestroy(wrapper::VulkanContext* vk_ctx);

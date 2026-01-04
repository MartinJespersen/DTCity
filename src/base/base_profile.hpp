#pragma once

#if defined(TRACY_PROFILE_ENABLE)
#include <vulkan/vulkan_core.h>
#include "third_party/tracy/tracy/TracyVulkan.hpp"
#include "third_party/tracy/tracy/Tracy.hpp"
#define prof_scope_marker ZoneScoped
#define prof_scope_marker_named(n) ZoneScopedN(n)
#define prof_frame_marker FrameMark;
#else
#define prof_scope_marker
#define prof_scope_marker_named(n)
#define prof_frame_marker ;
#endif

static U64
cpu_timer_freq_estimate();

static F64
us_from_cpu_cycles(U64 cycles);

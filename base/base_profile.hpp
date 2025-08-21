#pragma once
#include <vulkan/vulkan_core.h>
#include "third_party/tracy/tracy/TracyVulkan.hpp"
#if defined(PROFILE_ENABLE)
#include "third_party/tracy/tracy/Tracy.hpp"
#define ProfScopeMarker ZoneScoped
#define ProfScopeMarkerNamed(n) ZoneScopedN(n)
#define ProfFrameMarker FrameMark;
#else
#define ProfScopeMarker
#define ProfScopeMarkerNamed(n)
#define ProfFrameMarker ;
#endif

static U64
CpuTimerFreqEstimate(void);

struct Profiler
{
    U64 checkpoint;

    Profiler();
    ~Profiler();
};

#pragma once

#include "pch.hpp"

DISABLE_WARNINGS_PUSH
#define OS_FEATURE_GRAPHICAL 1

#include <memory>
#include <new>
#include <utility>
#include <type_traits>
#include <mimalloc.h>

// Shared base types and services. Optional base utilities belong to their consumers.
#include "base/base_context_cracking.h"
#include "base/base_core.hpp"
#include "base/base_profile.hpp"
#include "base/base_arena.hpp"
#include "base/base_allocator.hpp"
#include "base/base_math.h"
#include "base/base_strings.hpp"
#include "os_core/os_core_inc.hpp"
#include "base/base_thread.hpp"
#include "base/base_thread_context.h"
#include "base/container.hpp"

// Define debug event macros before parsing container template implementations.
#if !BUILD_TEST
#include "debug_log.hpp"
#endif
DISABLE_WARNINGS_POP

#include "base/base_allocator_templates.hpp"
#include "base/container_templates.hpp"

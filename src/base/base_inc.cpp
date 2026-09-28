// Copyright (c) 2024 Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

////////////////////////////////
//~ rjf: Base Includes

#undef MARKUP_LAYER_COLOR
#define MARKUP_LAYER_COLOR 0.20f, 0.60f, 0.80f

// The unity build includes this once per executable. ASan requires the tracking variant.
#if ASAN_ENABLED && !DTCITY_MIMALLOC_ASAN
#error ASan builds must link the ASan-enabled mimalloc target
#endif
#include <mimalloc-new-delete.h>

#include "debug_forward_ref.hpp"
#include "base_core.cpp"
#include "base_profile.cpp"
#include "base_arena.cpp"
#include "base_allocator.cpp"
#include "base_math.c"
#include "base_random.cpp"
#include "base_strings.cpp"

#include "base_thread_context.c"
// #include "base_command_line.c"
// #include "base_markup.c"
// #include "base_meta.c"
// #include "base_entry_point.c"
#include "container.cpp"
#include "base_freelist.cpp"
#include "os_core/os_core_inc.cpp"
#include "base_container.cpp"
#include "cache.cpp"
#include "base_lists.cpp"
#include "base_thread.cpp"

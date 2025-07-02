#pragma once

// user defined: [hpp]
#include "base/base_inc.hpp"
#include "os_core/os_core_inc.h"
#include "ui/ui.hpp"

const U32 MAX_FONTS_IN_USE = 10;

shared_function OS_Handle
Entrypoint(void* ptr);

shared_function void
Cleanup(void* ptr);

internal void
MainLoop(void* ptr);

internal void
CommandBufferRecord(U32 imageIndex, U32 currentFrame);

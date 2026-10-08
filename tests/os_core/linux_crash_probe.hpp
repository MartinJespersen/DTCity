#pragma once

#include "diagnostics.hpp"
#include "base/base_inc.hpp"
#include <sys/resource.h>

int
App(int argc, char** argv);

lib_internal void
_os_lnx_crash_probe_fail(void* ptr);
lib_internal void
_os_lnx_crash_probe_overflow(U32 depth);

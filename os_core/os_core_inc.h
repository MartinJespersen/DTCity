#ifndef OS_INC_H
#define OS_INC_H

#include "os_core/os_core.h"

#ifdef _WIN32
#include "os_core/win32/os_core_win32.h"
#elif defined(_GNUC_)
#include "os_core/linux/os_core_linux.h"
#else
#error OS core layer not implemented for this operating system.
#endif

#endif // OS_INC_H

#include "base/os_core/os_core.c"

#if _WIN64
#include "base/os_core/win32/os_core_win32.c"
#elif OS_LINUX
#include "base/os_core/linux/os_core_linux.c"
#else
#error OS core layer not implemented for this operating system.
#endif

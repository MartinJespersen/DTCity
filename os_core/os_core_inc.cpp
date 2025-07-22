#include "os_core/os_core.cpp"

#if OS_WINDOWS
#include "os_core/win32/os_core_win32.cpp"
#elif OS_LINUX
#include "os_core/linux/os_core_linux.c"
#else
#error OS core layer not implemented for this operating system.
#endif

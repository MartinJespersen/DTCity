// The Digital Grove Codebase
// Copyright (c) 2024 Ryan Fleury. All rights reserved.

// Undefine X11 macros that conflict with httplib
#ifdef Success
#undef Success
#endif
#include "http.c"

#if OS_WINDOWS
#include "win32/http_win32.c"
#elif OS_LINUX
#include "linux/http_linux.cpp"
#else
#error HTTP layer not implemented on this operating system.
#endif

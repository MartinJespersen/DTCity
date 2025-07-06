// The Digital Grove Codebase
// Copyright (c) 2024 Ryan Fleury. All rights reserved.

#include "http.c"

#if OS_WINDOWS
# include "win32/http_win32.c"
#else
# error HTTP layer not implemented on this operating system.
#endif

// The Digital Grove Codebase
// Copyright (c) 2024 Ryan Fleury. All rights reserved.

#ifndef HTTP_INC_H
#define HTTP_INC_H

#include "http.h"

#if OS_WINDOWS
#include "win32/http_win32.h"
#elif OS_LINUX
#include "linux/http_linux.hpp"

#else
#error HTTP layer not implemented on this operating system.
#endif

#endif // HTTP_INC_H

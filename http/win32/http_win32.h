// The Digital Grove Codebase
// Copyright (c) 2024 Ryan Fleury. All rights reserved.

#ifndef HTTP_WIN32_H
#define HTTP_WIN32_H

#include <winhttp.h>
#pragma comment(lib, "winhttp")

typedef struct HTTP_W32_State HTTP_W32_State;
struct HTTP_W32_State
{
 Arena *arena;
 HINTERNET hSession;
};

global HTTP_W32_State *http_w32_state = 0;

#endif // HTTP_WIN32_H

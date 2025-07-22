// The Digital Grove Codebase
// Copyright (c) 2024 Ryan Fleury. All rights reserved.

#ifndef HTTP_LNX_H
#define HTTP_LNX_H

#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "third_party/httplib.h"

typedef struct HTTP_LNX_State HTTP_LNX_State;
struct HTTP_LNX_State
{
    Arena* arena;
};

static HTTP_LNX_State* http_lnx_state = 0;

#endif // HTTP_LNX_H

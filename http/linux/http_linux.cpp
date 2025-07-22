// The Digital Grove Codebase
// Copyright (c) 2024 Ryan Fleury. All rights reserved.

////////////////////////////////////////////////////////////////
//~ rjf: Main Layer Initialization

#include "http/linux/http_linux.hpp"
#include "base/base_strings.hpp"
#include "base/error.hpp"
static void
HTTP_Init(void)
{
    if (http_lnx_state == 0)
    {
        Arena* arena = ArenaAlloc();
        http_lnx_state = PushArray(arena, HTTP_LNX_State, 1);
        http_lnx_state->arena = arena;
    }
}

////////////////////////////////////////////////////////////////
//~ rjf: Low-Level Request Functions

static HTTP_Response
HTTP_Request(Arena* arena, String8 url, String8 body, HTTP_RequestParams* params)
{
    httplib::Headers headers;

    httplib::Result res;
    // httplib::SSLClient cli((char*)url.str);
    httplib::Client cli((char*)"http://overpass-api.de");

    if (params->method == HTTP_Method_Post)
        res = cli.Post("/api/interpreter", (char*)body.str, (U32)body.size,
                       (char*)params->content_type.str);
    else
    {
        exitWithError("Only Get requests have been implemented for linux (works in windows but "
                      "left out for cross-platform support)");
    }
    HTTP_Response response;
    response.good = res.error() == httplib::Error::Success ? 1 : 0;
    if (!response.good)
    {
        printf("error from httplib: %s\n", httplib::to_string(res.error()).c_str());
    }
    else
    {
        response.code =
            response.good
                ? 200
                : 418; // TODO: convert error string from httplib::Error to HTTP_Status_Code
        response.body = PushStr8Copy(arena, Str8CString(res.value().body.c_str()));
    }
    return response;
}

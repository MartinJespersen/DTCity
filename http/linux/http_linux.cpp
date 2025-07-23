// The Digital Grove Codebase
// Copyright (c) 2024 Ryan Fleury. All rights reserved.

////////////////////////////////////////////////////////////////
//~ rjf: Main Layer Initialization

static void
HTTP_Init(void)
{
}

////////////////////////////////////////////////////////////////
//~ rjf: Low-Level Request Functions

static HTTP_Response
HTTP_Request(Arena* arena, String8 host, String8 path, String8 body, HTTP_RequestParams* params)
{
    httplib::Headers headers;

    httplib::Result res;
    httplib::Client cli((char*)host.str);

    if (params->method == HTTP_Method_Post)
        res = cli.Post((char*)path.str, (char*)body.str, (U32)body.size,
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

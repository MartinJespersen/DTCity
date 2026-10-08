#include "core_inc.hpp"
#include "async/async_inc.hpp"

namespace async
{

HttpInfo*
http_info_create(Arena* arena, HTTP_Method http_method, String8 http_path, String8 content_type,
                 std::initializer_list<String8> additional_headers, std::initializer_list<String8> params_list)
{
    String8List headers = {};
    for (auto header : additional_headers)
    {
        String8 str = push_str8_copy(arena, header);
        str8_list_push(arena, &headers, str);
    }

    String8List params = {};
    for (auto param : params_list)
    {
        String8 str = push_str8_copy(arena, param);
        str8_list_push(arena, &params, str);
    }

    HttpInfo* info = PushStruct(arena, HttpInfo);
    info->params = params;
    info->http_path = push_str8_copy(arena, http_path);
    info->content_type = push_str8_copy(arena, content_type);
    info->http_method = http_method;
    info->headers = headers;

    return info;
}

HttpInfo*
http_info_create_get(Arena* arena, String8 http_path, std::initializer_list<String8> additional_headers,
                     std::initializer_list<String8> params_list, String8 content_type)
{
    return http_info_create(arena, HTTP_Method_Get, http_path, content_type, additional_headers, params_list);
}

String8
_http_content_type_header_create(Arena* arena, String8 content_type)
{
    return push_str8f(arena, "Content-Type: %.*s", str8_varg(content_type));
}

g_internal void
async_http_global_init()
{
    static const CURLcode init_code = curl_global_init(CURL_GLOBAL_ALL);
    if (init_code != CURLE_OK)
    {
        exit_with_error("curl global init failed: %s", curl_easy_strerror(init_code));
    }
}

void
_curl_context_cleanup(CurlContext* curl_ctx)
{
    if (curl_ctx->added_to_multi && curl_ctx->multi_handle != 0 && curl_ctx->session_handle != 0)
    {
        curl_multi_remove_handle(curl_ctx->multi_handle, curl_ctx->session_handle);
        curl_ctx->added_to_multi = false;
    }
    if (curl_ctx->session_handle != 0)
    {
        curl_easy_cleanup(curl_ctx->session_handle);
        curl_ctx->session_handle = 0;
    }
    if (curl_ctx->multi_handle != 0)
    {
        curl_multi_cleanup(curl_ctx->multi_handle);
        curl_ctx->multi_handle = 0;
    }
    if (curl_ctx->headers != 0)
    {
        curl_slist_free_all(curl_ctx->headers);
        curl_ctx->headers = 0;
    }
    arena_release(curl_ctx->arena);
}

CurlContext*
_curl_ctx_create(Arena* arena)
{
    CurlContext* curl_ctx = PushStruct(arena, CurlContext);
    curl_ctx->arena = arena_alloc();
    Debug_SetName(curl_ctx->arena, "async HTTP curl arena");

    // curl library inits
    async_http_global_init();
    CURL* session_handle = curl_easy_init();
    CURLM* multi_handle = curl_multi_init();

    // create curl handles
    curl_ctx->session_handle = session_handle;
    curl_ctx->multi_handle = multi_handle;
    return curl_ctx;
}

void
_curl_reset(CurlContext* curl_ctx)
{
    if (curl_ctx->added_to_multi && curl_ctx->multi_handle != 0 && curl_ctx->session_handle != 0)
    {
        curl_multi_remove_handle(curl_ctx->multi_handle, curl_ctx->session_handle);
        curl_ctx->added_to_multi = false;
    }
    curl_easy_reset(curl_ctx->session_handle);
    curl_slist_free_all(curl_ctx->headers);
    curl_ctx->headers = 0;

    curl_ctx->chunk_list = {};
    arena_clear(curl_ctx->arena);
}

AsyncError
async_http_configure(Arena* arena, CurlContext* curl_ctx, HttpInfo* http_info)
{
    CURLU* url_handle = curl_url();
    defer(curl_url_cleanup(url_handle));
    if (curl_ctx->session_handle == 0 || curl_ctx->multi_handle == 0 || url_handle == 0)
    {
        return async_user_error(AsyncResult::HandleInitError);
    }

    B32 is_ws = str8_match(http_info->http_path, S("ws://"), MatchFlag_RightSideSloppy);
    B32 is_wss = str8_match(http_info->http_path, S("wss://"), MatchFlag_RightSideSloppy);
    B32 is_websocket = is_ws | is_wss;
    U32 url_flags = is_websocket ? CURLU_NON_SUPPORT_SCHEME : 0;

    CURLUcode url_err = curl_url_set(url_handle, CURLUPART_URL, (const char*)http_info->http_path.str, url_flags);
    if (url_err)
    {
        return async_curl_error(CurlCodeType::Url, url_err);
    }

    char* query_str = 0;
    if (http_info->params.node_count != 0)
    {
        for (String8Node* str_node = http_info->params.first; str_node; str_node = str_node->next)
        {
            String8 param_copy = push_str8_copy(arena, str_node->string);
            CURLUcode query_err = curl_url_set(url_handle, CURLUPART_QUERY, (const char*)param_copy.str,
                                               CURLU_APPENDQUERY | CURLU_URLENCODE);
            if (query_err)
            {
                return async_user_error(AsyncResult::QueryError);
            }
        }

        CURLUcode query_err = curl_url_get(url_handle, CURLUPART_QUERY, &query_str, 0);
        if (query_err)
        {
            return async_user_error(AsyncResult::QueryError);
        }
    }
    defer(if (query_str) { curl_free(query_str); });

    String8 request_url = push_str8_copy(arena, http_info->http_path);
    B32 append_params_to_url = http_info->http_method == HTTP_Method_Get;
    B32 passthrough_url = http_info->http_method == HTTP_Method_None;
    if (http_info->http_method == HTTP_Method_Post && http_info->params.node_count != 0 &&
        !str8_match(http_info->content_type, S("application/x-www-form-urlencoded"), 0))
    {
        append_params_to_url = true;
    }

    if (append_params_to_url)
    {
        char* url_str = 0;
        CURLUcode request_url_err = curl_url_get(url_handle, CURLUPART_URL, &url_str, 0);
        defer(if (url_str) { curl_free(url_str); });
        if (request_url_err)
        {
            return async_curl_error(CurlCodeType::Url, request_url_err);
        }
        request_url = push_str8_copy(arena, str8_c_string(url_str));
    }
    else if (!passthrough_url && http_info->http_method != HTTP_Method_Post)
    {
        return async_user_error(AsyncResult::InvalidMethodTypeError);
    }

    async_return_curl_error(CurlCodeType::Url,
                            curl_easy_setopt(curl_ctx->session_handle, CURLOPT_URL, (const char*)request_url.str));

    if (http_info->http_method == HTTP_Method_Get)
    {
        async_return_curl_error(CurlCodeType::Regular, curl_easy_setopt(curl_ctx->session_handle, CURLOPT_HTTPGET, 1L));
    }
    else if (http_info->http_method == HTTP_Method_Post)
    {
        String8 body = {};
        if (http_info->body.size != 0)
        {
            body = http_info->body;
        }
        if (body.size == 0 && query_str != 0 &&
            str8_match(http_info->content_type, S("application/x-www-form-urlencoded"), 0))
        {
            body = push_str8_copy(arena, str8_c_string(query_str));
        }

        async_return_curl_error(CurlCodeType::Regular, curl_easy_setopt(curl_ctx->session_handle, CURLOPT_POST, 1L));
        async_return_curl_error(CurlCodeType::Regular,
                                curl_easy_setopt(curl_ctx->session_handle, CURLOPT_POSTFIELDS, body.str));
        async_return_curl_error(
            CurlCodeType::Regular,
            curl_easy_setopt(curl_ctx->session_handle, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)body.size));
    }

    curl_slist* headers = 0;
    if (http_info->content_type.size)
    {
        String8 content_type_header = _http_content_type_header_create(arena, http_info->content_type);
        headers = curl_slist_append(headers, (const char*)content_type_header.str);
    }
    for (String8Node* header_str = http_info->headers.first; header_str; header_str = header_str->next)
    {
        headers = curl_slist_append(headers, (const char*)header_str->string.str);
    }

    curl_ctx->headers = headers;

    async_return_curl_error(CurlCodeType::Regular,
                            curl_easy_setopt(curl_ctx->session_handle, CURLOPT_HTTPHEADER, curl_ctx->headers));

    CURLMcode multi_err = curl_multi_add_handle(curl_ctx->multi_handle, curl_ctx->session_handle);
    if (multi_err != CURLM_OK)
    {
        return async_curl_error(CurlCodeType::Multi, multi_err);
    }
    curl_ctx->added_to_multi = true;

    return async_no_error();
}

} // namespace async

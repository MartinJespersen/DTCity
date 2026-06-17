namespace async
{

g_internal void
_ws_main(AsyncWebsocketSession* ws_session)
{
    CurlContext* curl_ctx = ws_session->curl_ctx;
    int running = 1;
    CURLMcode curl_code = curl_multi_perform(curl_ctx->multi_handle, &running);

    if (curl_code != CURLM_OK)
    {
        async_error_set(ws_session, async_curl_regular_error(curl_code));
    }
    else if (!running)
    {
        S32 msgs_left = 0;
        B32 found_msg = 0;
        CURLMsg* msg = curl_multi_info_read(curl_ctx->multi_handle, &msgs_left);
        for (; msg; msg = curl_multi_info_read(curl_ctx->multi_handle, &msgs_left))
        {
            if (msg->msg == CURLMSG_DONE && msg->easy_handle == curl_ctx->session_handle)
            {
                found_msg = true;
                break;
            }
        }
        if (found_msg == false)
        {
            return;
        }

        CURLcode result = msg->data.result;
        long http_code = 0;
        curl_easy_getinfo(msg->easy_handle, CURLINFO_RESPONSE_CODE, &http_code);

        if (result != CURLE_OK)
        {
            S32 error_code = result ? result : http_code;
            async_error_set(ws_session, async_curl_regular_error(error_code));
        }
        else
        {
            if (http_code >= 400)
            {
                ws_session->http_error_code = (U32)http_code;
                async_error_set(ws_session, async_user_error(AsyncResult::HttpError));
            }
        }
    }
}

g_internal AsyncWebsocketCreateResult
async_websocket_start(String8 url)
{
    Arena* session_arena = arena_alloc();
    AsyncWebsocketSession* ws_session = PushStruct(session_arena, AsyncWebsocketSession);
    ws_session->arena = session_arena;
    ws_session->msg_rw_mutex = os_rw_mutex_alloc();
    ws_session->msg_arena = arena_alloc();
    async_http_global_init();
    ws_session->curl_ctx = _curl_ctx_create(session_arena);
    async::HttpInfo* http_info = async::http_info_create(session_arena, HTTP_Method_None, url, {}, {}, {});
    // websocket extension
    {
        B32 is_ws = str8_match(http_info->http_path, S("ws://"), MatchFlag_RightSideSloppy);
        B32 is_wss = str8_match(http_info->http_path, S("wss://"), MatchFlag_RightSideSloppy);
        B32 is_websocket = is_ws | is_wss;
        if (!is_websocket)
        {
            AsyncError error = async_user_error(AsyncResult::Expecting_Websocket);
            AsyncWebsocketCreateResult result = AsyncWebsocketCreateResult(error, ws_session);
            return result;
        }
    }

    ws_session->error = _async_http_configure(session_arena, ws_session->curl_ctx, http_info, _libcurl_ws_callback, ws_session);

    AsyncWebsocketCreateResult result = AsyncWebsocketCreateResult(ws_session->error, ws_session);
    return result;
}

g_internal void
async_http_connection_end(AsyncWebsocketSession* ws_session)
{
    if (ws_session == 0)
    {
        return;
    }

    os_rw_mutex_release(ws_session->msg_rw_mutex);
    _curl_context_cleanup(ws_session->curl_ctx);

    arena_release(ws_session->msg_arena);
    arena_release(ws_session->arena);
}

g_internal String8List
async_websocket_read(Arena* arena, AsyncWebsocketSession* ws_session)
{
    String8List result = {};

    _ws_main(ws_session);
    if (ws_session->error.has_error())
    {
        DEBUG_LOG("error when reading websocket: %u", ws_session->error.curl_code);
    }

    os_mutex_scope_w(ws_session->msg_rw_mutex)
    {
        String8List* ws_msgs = &ws_session->msg_list;
        result = str8_list_copy(arena, ws_msgs);
        *ws_msgs = {};
        arena_clear(ws_session->msg_arena);
    }
    return result;
}

g_internal size_t
_libcurl_ws_callback(void* contents, size_t size, size_t nmemb, void* userp)
{
    size_t total_size = size * nmemb;
    AsyncWebsocketSession* ws_session = (AsyncWebsocketSession*)userp;
    CurlContext* curl_ctx = ws_session->curl_ctx;
    Arena* msg_arena = ws_session->msg_arena;

    U8* buffer = PushArray(msg_arena, U8, total_size);
    ChunkItem<U8>* chunk = chunk_item_from_array(msg_arena, buffer, total_size);
    MemoryCopy(buffer, contents, total_size);
    chunk_list_insert_chunk(&curl_ctx->chunk_list, chunk);

    const struct curl_ws_frame* m = curl_ws_meta(curl_ctx->session_handle);
    bool frame_done = m->bytesleft == 0;
    bool message_done = frame_done && !(m->flags & CURLWS_CONT);
    if (message_done)
    {
        os_mutex_scope_w(ws_session->msg_rw_mutex)
        {
            String8 msg = str8_from_chunk_list(ws_session->msg_arena, &curl_ctx->chunk_list);
            str8_list_push(msg_arena, &ws_session->msg_list, msg);
        }

        // reset chunk list
        arena_clear(curl_ctx->arena);
        curl_ctx->chunk_list = {};
    }

    return total_size;
}
} // namespace async

#include "core_inc.hpp"
#include "async/async_inc.hpp"

namespace async
{

g_internal size_t
_libcurl_ws_write_callback(void* contents, size_t size, size_t nmemb, void* userp);
g_internal AsyncError
_async_websocket_configure(AsyncWebsocketSession* ws_session);
g_internal void
_async_websocket_send_clear(AsyncWebsocketSession* ws_session);

g_internal void
_ws_main(AsyncWebsocketSession* ws_session)
{
    CurlContext* curl_ctx = ws_session->curl_ctx;
    B32 has_outgoing = false;
    os_mutex_scope_r(ws_session->msg_rw_mutex)
    {
        has_outgoing = ws_session->send_first != 0;
    }
    if (ws_session->send_paused && has_outgoing)
    {
        ws_session->send_paused = false;
        // Resume only on the curl owner thread, without holding the queue lock.
        CURLcode resume_result = curl_easy_pause(curl_ctx->session_handle, CURLPAUSE_CONT);
        if (resume_result != CURLE_OK)
        {
            async_error_set(ws_session, async_curl_regular_error(resume_result));
            return;
        }
    }
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
            else
            {
                async_error_set(ws_session, async_user_error(AsyncResult::WebsocketDisconnected));
            }
        }
    }
}
WebsocketConnection
async_websocket_start(String8 url)
{
    Arena* session_arena = arena_alloc();
    Debug_SetName(session_arena, "websocket session arena");
    AsyncWebsocketSession* ws_session = PushStruct(session_arena, AsyncWebsocketSession);
    ws_session->arena = session_arena;
    ws_session->msg_rw_mutex = os_rw_mutex_alloc();
    ws_session->msg_read_arena = arena_alloc();
    Debug_SetName(ws_session->msg_read_arena, "websocket message arena");

    ws_session->curl_ctx = _curl_ctx_create(session_arena);
    ws_session->http_info = async::http_info_create(session_arena, HTTP_Method_None, url, {}, {}, {});
    // websocket extension
    {
        B32 is_ws = str8_match(ws_session->http_info->http_path, S("ws://"), MatchFlag_RightSideSloppy);
        B32 is_wss = str8_match(ws_session->http_info->http_path, S("wss://"), MatchFlag_RightSideSloppy);
        B32 is_websocket = is_ws | is_wss;
        if (!is_websocket)
        {
            AsyncError error = async_user_error(AsyncResult::Expecting_Websocket);
            WebsocketConnection result = WebsocketConnection(error, ws_session);
            return result;
        }
    }

    ws_session->error = _async_websocket_configure(ws_session);

    WebsocketConnection result = WebsocketConnection(ws_session->error, ws_session);
    return result;
}

void
_async_websocket_connection_end(AsyncWebsocketSession* ws_session)
{
    if (ws_session == 0)
    {
        return;
    }

    _curl_context_cleanup(ws_session->curl_ctx);
    _async_websocket_send_clear(ws_session);

    os_rw_mutex_release(ws_session->msg_rw_mutex);

    arena_release(ws_session->msg_read_arena);
    arena_release(ws_session->arena);
}

bool
WebsocketConnection::try_send_resv(Arena* arena, String8List* msg_send_list, String8List* msg_recv_list)
{
    bool connection_established = false;
    for (String8Node* msg_node = msg_send_list->first; msg_node; msg_node = msg_node->next)
    {
        Arena* msg_arena = arena_alloc();
        WebsocketPayload* payload = PushStruct(msg_arena, WebsocketPayload);
        payload->arena = msg_arena;
        payload->msg = push_str8_copy(msg_arena, msg_node->string);
        os_mutex_scope_w(this->ws_session->msg_rw_mutex)
        {
            SLLQueuePush(this->ws_session->send_first, this->ws_session->send_last, payload);
        }
    }

    _ws_main(ws_session);
    AsyncError update_error = ws_session->error;
    if (update_error.has_error())
    {
        DEBUG_LOG("error when updating websocket: %u (%s)", ws_session->error.curl_code, ws_session->curl_error);
        _curl_reset(ws_session->curl_ctx);
        // Delivery is unknown after disconnect: do not replay possibly sent messages.
        _async_websocket_send_clear(ws_session);
        ws_session->send_paused = false;
        ws_session->handshake_complete = false;
        ws_session->error = _async_websocket_configure(ws_session);
    }
    else if (!ws_session->handshake_complete)
    {
        // Configuration is not a connection: wait until curl accepts the WebSocket upgrade.
        long http_code = 0;
        CURLcode info_result =
            curl_easy_getinfo(ws_session->curl_ctx->session_handle, CURLINFO_RESPONSE_CODE, &http_code);
        if (info_result == CURLE_OK && http_code == 101)
        {
            ws_session->handshake_complete = true;
            connection_established = true;
        }
    }

    // Reconnect configuration must not hide the transport failure from this update.
    this->async_result = update_error;
    String8List result = {};

    os_mutex_scope_w(ws_session->msg_rw_mutex)
    {
        String8List* ws_msgs = &ws_session->msg_list;
        result = str8_list_copy(arena, ws_msgs);
        *ws_msgs = {};
        arena_clear(ws_session->msg_read_arena);
    }

    *msg_recv_list = result;
    return connection_established;
}

g_internal size_t
_libcurl_ws_read_callback(void* contents, size_t size, size_t nmemb, void* userp)
{
    size_t total_size = size * nmemb;
    AsyncWebsocketSession* ws_session = (AsyncWebsocketSession*)userp;
    CurlContext* curl_ctx = ws_session->curl_ctx;

    const struct curl_ws_frame* m = curl_ws_meta(curl_ctx->session_handle);
    if (!m)
    {
        return CURL_WRITEFUNC_ERROR;
    }
    // Control frames must not be appended to an in-progress data message.
    if (m->flags & (CURLWS_PING | CURLWS_PONG | CURLWS_CLOSE))
    {
        return total_size;
    }
    U8* buffer = PushArray(curl_ctx->arena, U8, total_size);
    ChunkItem<U8>* chunk = chunk_item_from_array(curl_ctx->arena, buffer, total_size);
    MemoryCopy(buffer, contents, total_size);
    chunk_list_insert_chunk(&curl_ctx->chunk_list, chunk);

    bool frame_done = m->bytesleft == 0;
    bool message_done = frame_done && !(m->flags & CURLWS_CONT);
    if (message_done)
    {
        os_mutex_scope_w(ws_session->msg_rw_mutex)
        {
            Arena* msg_arena = ws_session->msg_read_arena;
            String8 msg = str8_from_chunk_list(msg_arena, &curl_ctx->chunk_list);
            str8_list_push(msg_arena, &ws_session->msg_list, msg);
        }

        // reset chunk list
        arena_clear(curl_ctx->arena);
        curl_ctx->chunk_list = {};
    }

    return total_size;
}

g_internal size_t
_libcurl_ws_write_callback(void* contents, size_t size, size_t nmemb, void* userp)
{
    AsyncWebsocketSession* ws_session = (AsyncWebsocketSession*)userp;
    os_rw_mutex_take_w(ws_session->msg_rw_mutex);
    defer(os_rw_mutex_drop_w(ws_session->msg_rw_mutex));
    WebsocketPayload* payload = ws_session->send_first;
    if (!payload)
    {
        ws_session->send_paused = true;
        return CURL_READFUNC_PAUSE;
    }
    if (!payload->frame_started)
    {
        CURLcode result =
            curl_ws_start_frame(ws_session->curl_ctx->session_handle, CURLWS_TEXT, (curl_off_t)payload->msg.size);
        if (result != CURLE_OK)
        {
            async_error_set(ws_session, async_curl_regular_error(result));
            return CURL_READFUNC_ABORT;
        }
        payload->frame_started = true;
    }
    size_t copied = (size_t)Min((U64)(size * nmemb), payload->msg.size - payload->offset);
    if (copied > 0)
    {
        MemoryCopy(contents, payload->msg.str + payload->offset, copied);
    }
    payload->offset += copied;
    if (payload->offset == payload->msg.size)
    {
        SLLQueuePop(ws_session->send_first, ws_session->send_last);
        arena_release(payload->arena);
    }
    // After start_frame, zero bytes correctly represents an empty text frame.
    return copied;
}

g_internal AsyncError
_async_websocket_configure(AsyncWebsocketSession* ws_session)
{
    CurlContext* curl_ctx = ws_session->curl_ctx;
    AsyncError error = async_http_configure(ws_session->arena, curl_ctx, ws_session->http_info);
    if (error.has_error())
    {
        return error;
    }
    ws_session->curl_error[0] = 0;
    async_return_curl_error(CurlCodeType::Regular,
                            curl_easy_setopt(curl_ctx->session_handle, CURLOPT_ERRORBUFFER, ws_session->curl_error));
    async_return_curl_error(CurlCodeType::Regular, curl_easy_setopt(curl_ctx->session_handle, CURLOPT_WRITEFUNCTION,
                                                                    _libcurl_ws_read_callback));
    async_return_curl_error(CurlCodeType::Regular,
                            curl_easy_setopt(curl_ctx->session_handle, CURLOPT_WRITEDATA, ws_session));
    async_return_curl_error(CurlCodeType::Regular, curl_easy_setopt(curl_ctx->session_handle, CURLOPT_READFUNCTION,
                                                                    _libcurl_ws_write_callback));
    async_return_curl_error(CurlCodeType::Regular,
                            curl_easy_setopt(curl_ctx->session_handle, CURLOPT_READDATA, ws_session));
    async_return_curl_error(CurlCodeType::Regular, curl_easy_setopt(curl_ctx->session_handle, CURLOPT_UPLOAD, 1L));
    return async_no_error();
}

g_internal void
_async_websocket_send_clear(AsyncWebsocketSession* ws_session)
{
    os_mutex_scope_w(ws_session->msg_rw_mutex)
    {
        while (ws_session->send_first)
        {
            WebsocketPayload* payload = ws_session->send_first;
            SLLQueuePop(ws_session->send_first, ws_session->send_last);
            arena_release(payload->arena);
        }
    }
}

} // namespace async

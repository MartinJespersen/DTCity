namespace async
{

struct WebsocketPayload;

struct AsyncWebsocketSession
{
    Arena* arena;

    // error
    AsyncError error;

    // curl
    CurlContext* curl_ctx;
    char curl_error[CURL_ERROR_SIZE];

    // http
    HttpInfo* http_info;
    U32 http_error_code;
    String8 http_error_msg;

    // msgs
    OS_Handle msg_rw_mutex;
    Arena* msg_read_arena; // reset every read
    String8List msg_list;  // Protected by msg_rw_mutex.
    WebsocketPayload* send_first;
    WebsocketPayload* send_last;
    B32 send_paused;        // Accessed only by the thread pumping read().
    B32 handshake_complete; // Reset for each transport connection.
};

struct WebsocketPayload
{
    WebsocketPayload* next;
    Arena* arena;
    String8 msg;
    U64 offset;
    B32 frame_started;
};

g_internal void
_async_websocket_connection_end(AsyncWebsocketSession* ws_session);

struct WebsocketConnection
{
  private:
    AsyncWebsocketSession* ws_session;

  public:
    AsyncError async_result;

    WebsocketConnection() = default;

    WebsocketConnection(AsyncError async_result, AsyncWebsocketSession* ws_session)
    {
        this->async_result = async_result;
        this->ws_session = ws_session;
    }

    ~WebsocketConnection()
    {
        if (this->ws_session)
        {
            _async_websocket_connection_end(this->ws_session);
        }
    }

    WebsocketConnection(const WebsocketConnection& other) = delete;
    WebsocketConnection&
    operator=(const WebsocketConnection& other) = delete;

    WebsocketConnection(WebsocketConnection&& other) noexcept
    {
        this->async_result = other.async_result;
        this->ws_session = other.ws_session;

        other.async_result = async_no_error();
        other.ws_session = 0;
    }

    WebsocketConnection&
    operator=(WebsocketConnection&& other) noexcept
    {
        if (this != &other)
        {
            _async_websocket_connection_end(this->ws_session);

            this->async_result = other.async_result;
            this->ws_session = other.ws_session;

            other.async_result = async_no_error();
            other.ws_session = 0;
        }
        return *this;
    }

    bool
    has_error()
    {
        return async_result.result != AsyncResult::Success;
    }

    // Optionally queue a text message, progress curl, and return received messages in arena.
    // Call regularly on the connection's owner thread; pending sends remain queued.
    // connection_established is true once per successful handshake, including reconnects.
    bool
    try_send_resv(Arena* arena, String8List* msg_send_list, String8List* msg_recv_msgs);
};

g_internal WebsocketConnection
async_websocket_start(String8 url);

g_internal size_t
_libcurl_ws_read_callback(void* contents, size_t size, size_t nmemb, void* userp);

} // namespace async

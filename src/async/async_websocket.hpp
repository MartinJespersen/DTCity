namespace async
{

struct AsyncWebsocketSession
{
    Arena* arena;

    // error
    AsyncError error;

    // curl
    CurlContext* curl_ctx;

    // http
    U32 http_error_code;
    String8 http_error_msg;

    // msgs
    OS_Handle msg_rw_mutex;
    Arena* msg_arena;     // reset every read
    String8List msg_list; // IMPORTANT: accessed from multiple thread and should be protected by mutex
};

g_internal void
_async_http_connection_end(AsyncWebsocketSession* ws_session);

g_internal String8List
_async_websocket_read(Arena* arena, AsyncWebsocketSession* ws_session);

struct WebsocketConnection
{
  private:
    AsyncWebsocketSession* ws_session;

  public:
    AsyncError async_result;

    WebsocketConnection()
    {
        this->async_result = async_no_error();
        this->ws_session = 0;
    }

    WebsocketConnection(AsyncError async_result, AsyncWebsocketSession* ws_session)
    {
        this->async_result = async_result;
        this->ws_session = ws_session;
    }

    ~WebsocketConnection()
    {
        _async_http_connection_end(this->ws_session);
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
            _async_http_connection_end(this->ws_session);

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

    String8List
    read(Arena* arena);
};

g_internal WebsocketConnection
async_websocket_start(String8 url);

g_internal size_t
_libcurl_ws_callback(void* contents, size_t size, size_t nmemb, void* userp);
} // namespace async

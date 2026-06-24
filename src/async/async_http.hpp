
namespace async
{
typedef void* AsyncHandle;

// forward declarations
template <typename T>
struct AsyncHttpTaskState;
struct HttpInfo;
template <typename T>
struct UserFuncResult;

// typedefs for worker funcs
template <typename T>
using AsyncWorkFunc = UserFuncResult<T> (*)(Arena* arena, async::ThreadPool*, String8 response_body, T* task_state);
template <typename T>
using MainThreadWorkFunc = void (*)(AsyncTaskStatus<T>* task_state);

struct HttpInfo
{
    String8List params;
    String8 http_path;
    String8 content_type;
    HTTP_Method http_method;
    String8List headers;
    String8 body;
};

template <typename T>
struct UserFuncResult
{
    String8 msg;
    B32 successful;
    B32 to_reschedule;
    S64 us_delay;
    MainThreadWorkFunc<T> main_thread_func;
    HttpInfo* http_info;
    AsyncWorkFunc<T> next_func;
    AsyncTaskContinuation<T> next_task;

    static UserFuncResult<T>
    reschedule(S64 us_delay = 0)
    {
        UserFuncResult<T> result = {.to_reschedule = true, .us_delay = us_delay};
        return result;
    }

    static UserFuncResult<T>
    failure(Arena* arena, const char* msg, ...)
    {
        va_list args;
        va_start(args, msg);
        String8 msg_final = push_str8fv(arena, msg, args);
        va_end(args);

        UserFuncResult<T> result = {.msg = msg_final};
        return result;
    }

    static UserFuncResult<T>
    success(HttpInfo* http_info, AsyncWorkFunc<T> next_func)
    {
        UserFuncResult<T> result = {.successful = true, .http_info = http_info, .next_func = next_func};
        return result;
    }

    static UserFuncResult<T>
    success()
    {
        UserFuncResult<T> result = {.successful = true};
        return result;
    }
    static UserFuncResult<T>
    success(MainThreadWorkFunc<T> main_thread_func)
    {
        UserFuncResult<T> result = {.successful = true, .main_thread_func = main_thread_func};
        return result;
    }

    static UserFuncResult<T>
    success(AsyncTaskContinuation<T> next_task)
    {
        UserFuncResult<T> result = {.successful = true, .next_task = next_task};
        return result;
    }
};

struct KeyValue
{
    String8 k;
    String8 v;
};

enum class AsyncResult : U32
{
    Success,
    HandleInitError,
    QueryError,
    UrlError,
    InvalidContentTypeError,
    InvalidMethodTypeError,
    FormDataError,
    NoWorkError,
    CurlError,
    HttpError,
    UserFunctionError,
    TimeoutError,
    Expecting_Websocket,
    WebsocketDisconnected

};

enum class CurlCodeType : U32
{
    None,
    Regular,
    Url,
    Multi
};

struct AsyncError
{
    AsyncResult result;
    CurlCodeType curl_code_type;
    U32 curl_code;
    const char* file;
    U32 line;
    String8 error_str;

    bool
    has_error()
    {
        return result != AsyncResult::Success;
    }
};

g_internal AsyncError
async_error(AsyncResult result, CurlCodeType curl_code_type, U32 curl_code, const char* file, U32 line, String8 error_msg)
{
    return {result, curl_code_type, curl_code, file, line, error_msg};
}
// clang-format off
#define async_error(r, ct, c, m) async_error(r, ct, c, __FILE__, __LINE__, m)
#define async_user_error(r) async_error(r,CurlCodeType::None, 0, {})
#define async_user_error_with_msg(r, m) async_error(r,CurlCodeType::None, 0, m)
#define async_no_error() async_user_error(AsyncResult::Success)
#define async_curl_error(ct, c) async_error(AsyncResult::CurlError, ct, c, {})
#define async_return_curl_error(ct, c) if (c != 0) return async_curl_error(ct, c)
#define async_curl_regular_error(c) async_curl_error(CurlCodeType::Regular, c)
#define async_error_set(o, e) ((o)->error = (e))
// clang-format on

typedef size_t (*CurlWriteCallback)(void* contents, size_t size, size_t nmemb, void* userp);
struct CurlContext
{
    Arena* arena;
    ChunkList<U8> chunk_list;
    CURLM* multi_handle;
    CURL* session_handle;
    curl_slist* headers;
    B32 added_to_multi;
};

template <typename T>
struct AsyncHttpTaskState
{
    U32 cur_task_retry_count;
    U32 max_task_retries;
    U32 cur_http_retry_count;
    U32 max_http_retries;

    // keep alive
    U32 timeout_sec;
    U64 timeout_us;
    U64 task_start_us;

    // first scheduled work
    ThreadPool* thread_pool;

    AsyncWorkFunc<T> first_func;
    AsyncWorkFunc<T> next_func;

    HttpInfo* first_http_info;
    HttpInfo* next_http_info;

    // curl
    CurlContext* curl_ctx;

    // errors ////////////////////////
    AsyncError error;
    U32 http_error_code;
    String8 http_error_msg;
};

template <typename T>
struct AsyncHttpTaskStateConfig
{
    AsyncWorkFunc<T> first_func;
    T* user_data;
    U32 max_http_retries;
    U32 max_task_retries;
    U32 timeout_sec;

    AsyncHttpTaskStateConfig(AsyncWorkFunc<T> first_func, T* user_data, U32 max_http_retries, U32 max_task_retries, U32 timeout_sec = 0)
        : first_func(first_func), max_http_retries(max_http_retries), max_task_retries(max_task_retries), timeout_sec(timeout_sec), user_data(user_data)
    {
    }
};

template <typename T>
struct AsyncHttpTaskCreateResult
{
    AsyncError async_result;
    AsyncTaskStatus<T>* task_state;
};

template <typename T>
struct MainThreadTaskState
{
    MainThreadWorkFunc<T> main_thread_func;
    AsyncTaskStatus<T>* task_state;
};

template <typename T>
struct LibCurlCallbackData
{
    Arena* arena;
    AsyncHttpTaskState<T>* http_ctx;
};

g_internal HttpInfo*
http_info_create(Arena* arena, HTTP_Method http_method, String8 http_path, String8 content_type, std::initializer_list<String8> additional_headers, std::initializer_list<String8> params_list);

g_internal HttpInfo*
http_info_create_get(Arena* arena, String8 http_path, std::initializer_list<String8> additional_headers = {}, std::initializer_list<String8> params_list = {}, String8 content_type = {});

g_internal String8
_http_content_type_header_create(Arena* arena, String8 content_type);

g_internal void
async_http_global_init();

template <typename T>
g_internal size_t
_libcurl_callback(void* contents, size_t size, size_t nmemb, void* userp);

template <typename T>
g_internal AsyncTaskContinuation<T>
_http_main(ThreadInfo thread_info, AsyncTaskStatus<T>* task_status);

template <typename T>
g_internal AsyncTaskContinuation<T>
_async_http_task_continuation(AsyncHttpTaskState<T>* http_ctx, AsyncWorkFunc<T> func, HttpInfo* http_info, S64 us_delay = 0);

template <typename T>
g_internal void
_async_thread_pool_push(ThreadPool* thread_pool, AsyncTaskStatus<T>* task, AsyncWorkFunc<T> func, HttpInfo* http_info, S64 us_delay = 0);

template <typename T>
WorkerResult
_main_thread_func(ThreadInfo thread_info, WorkerData data);

template <typename T>
g_internal B32
_async_main_thread_queue_push(ThreadPool* thread_pool, AsyncTaskStatus<T>* task_state, MainThreadWorkFunc<T> func);

g_internal CurlContext*
_curl_ctx_create(Arena* arena);

g_internal void
_curl_reset(CurlContext* curl_ctx);

g_internal void
_curl_context_cleanup(CurlContext* curl_ctx);

template <typename T>
g_internal AsyncError
_async_http_configure(Arena* arena, AsyncHttpTaskState<T>* http_ctx, HttpInfo* http_info);

g_internal AsyncError
_async_http_configure(Arena* arena, CurlContext* curl_ctx, HttpInfo* http_info, CurlWriteCallback curl_write_callback, void* callback_data);

template <typename T>
g_internal AsyncHttpTaskCreateResult<T>
async_http_task_run(Arena* arena, ThreadPool* thread_pool, HttpInfo* http_info, AsyncHttpTaskStateConfig<T>* config, String8 task_name);

template <typename T>
g_internal AsyncHttpTaskCreateResult<T>
async_http_task_run(ThreadPool* thread_pool, HttpInfo* http_info, AsyncHttpTaskStateConfig<T>* config, String8 task_name);

} // namespace async

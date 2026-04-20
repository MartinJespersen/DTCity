
namespace async
{
typedef void* AsyncHandle;

// forward declarations
template <typename T>
struct AsyncTaskState;
template <typename T>
struct HttpInfo;
template <typename T>
struct UserFuncResult;

// typedefs for worker funcs
template <typename T>
using AsyncWorkFunc = UserFuncResult<T> (*)(Arena* arena, async::ThreadPool*, String8 response_body, T* task_state);
template <typename T>
using MainThreadWorkFunc = void (*)(AsyncTaskState<T>* task_state);
template <typename T>
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
    HttpInfo<T>* http_info;
    AsyncWorkFunc<T> next_func;

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
    success(HttpInfo<T>* http_info, AsyncWorkFunc<T> next_func)
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
    TimeoutError

};

#define OrReturnError(err, async_error, ctx, line, file)                                                                                                                                               \
    do                                                                                                                                                                                                 \
    {                                                                                                                                                                                                  \
        AsyncResult error = (AsyncResult)(async_error);                                                                                                                                                \
        if ((bool)(err))                                                                                                                                                                               \
        {                                                                                                                                                                                              \
            (ctx)->error_code = (U32)(err);                                                                                                                                                            \
            (ctx)->err_line = (line);                                                                                                                                                                  \
            (ctx)->err_file = (file);                                                                                                                                                                  \
            return (error);                                                                                                                                                                            \
        }                                                                                                                                                                                              \
    } while (0)

struct CurlContext
{
    WriteChunkList chunk_list;
    CURLM* multi_handle;
    CURL* session_handle;
    curl_slist* headers;
    B32 added_to_multi;
};

template <typename T>
struct AsyncTaskState
{
    Arena* arena;

    String8 task_name;

    B32 started;
    std::atomic<B32> done;
    std::atomic<B32> success;
    U32 cur_task_retry_count;
    U32 max_task_retries;
    U32 cur_http_retry_count;
    U32 max_http_retries;

    // keep alive
    U64 timeout_us;
    U64 task_start_us;

    // first scheduled work
    AsyncWorkFunc<T> first_func;
    HttpInfo<T>* first_http_info;

    // curl
    CurlContext curl_ctx;

    // task specific state
    T task_state;

    // errors ////////////////////////
    AsyncResult async_result;
    U32 err_line;
    const char* err_file;
    U32 error_code;
    String8 error_str;
};

template <typename T>
struct AsyncHttpTaskCreateResult
{
    AsyncResult async_result;
    std::shared_ptr<AsyncTaskState<T>> task_state;
};

template <typename T>
struct AsyncCallbackArg
{
    AsyncTaskState<T>* ctx;
    AsyncWorkFunc<T> func;
    HttpInfo<T>* http_info;
};

template <typename T>
struct MainThreadTaskState
{
    AsyncTaskState<T>* task_state;
    MainThreadWorkFunc<T> main_thread_func;
};

template <typename T>
struct AsyncCallCtxDeleter
{
    void
    operator()(AsyncTaskState<T>* ctx) const
    {
        // Either wait here, or assert that the caller already waited.
        AssertAlways(ctx->done.load(std::memory_order_acquire));

        _curl_context_cleanup(&ctx->curl_ctx);

        Arena* arena = ctx->arena;
        arena_release(arena);
    }
};

template <typename T>
g_internal HttpInfo<T>*
http_info_create(Arena* arena, HTTP_Method http_method, String8 http_path, String8 content_type, std::initializer_list<String8> additional_headers, std::initializer_list<String8> params_list);

template <typename T>
g_internal HttpInfo<T>*
http_info_create_get(Arena* arena, String8 http_path, std::initializer_list<String8> additional_headers = {}, std::initializer_list<String8> params_list = {}, String8 content_type = {});

g_internal String8
_http_content_type_header_create(Arena* arena, String8 content_type);

g_internal void
async_http_global_init();

g_internal void
_curl_context_cleanup(CurlContext* curl_ctx);

template <typename T>
g_internal size_t
_libcurl_callback(void* contents, size_t size, size_t nmemb, void* userp);

template <typename T>
g_internal void
_write_data_complete(ThreadInfo thread_info, void* user_data);

template <typename T>
g_internal void
_async_thread_pool_push(ThreadPool* thread_pool, AsyncTaskState<T>* ctx, AsyncWorkFunc<T> func, HttpInfo<T>* http_info, S64 us_delay = 0);

template <typename T>
void
_main_thread_func(ThreadInfo thread_info, void* data);

template <typename T>
g_internal B32
_async_main_thread_queue_push(ThreadPool* thread_pool, AsyncTaskState<T>* task_state, MainThreadWorkFunc<T> func);

template <typename T>
g_internal void
_error_set(AsyncResult error_code, const char* file, S32 line, AsyncTaskState<T>* ctx, String8 error_msg = {});

template <typename T>
g_internal void
_error_reset(AsyncTaskState<T>* ctx);

template <typename T>
g_internal void
_curl_error_set(S32 curl_error_code, const char* file, S32 line, AsyncTaskState<T>* ctx);

g_internal void
_curl_reset(CurlContext* curl_ctx);

template <typename T>
g_internal AsyncResult
_async_http_configure(AsyncTaskState<T>* async_ctx, HttpInfo<T>* http_info);

template <typename T>
g_internal std::shared_ptr<AsyncTaskState<T>>
async_task_state_create(String8 name);

template <typename T>
g_internal AsyncResult
async_http_task_create(const std::shared_ptr<AsyncTaskState<T>>& async_ctx, ThreadPool* thread_pool, HTTP_Method http_method, String8 http_path, String8 content_type, String8 body,
                       std::initializer_list<String8> params, std::initializer_list<String8> additional_headers_list, AsyncWorkFunc<T> func, U32 max_http_retry_count = 0, U32 task_retry_count = 0,
                       U32 timeout_sec = 300);

} // namespace async

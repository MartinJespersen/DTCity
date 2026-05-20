
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

struct AsyncHttpResult
{
    AsyncResult async_result;
    U32 error_code;
    String8 error_str;
};

#define OrReturnError(err, async_error, ctx, line, file)                                                                                                                                               \
    do                                                                                                                                                                                                 \
    {                                                                                                                                                                                                  \
        AsyncResult error = (AsyncResult)(async_error);                                                                                                                                                \
        if ((bool)(err))                                                                                                                                                                               \
        {                                                                                                                                                                                              \
            (ctx)->http_result.error_code = (U32)(err);                                                                                                                                                \
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
    CurlContext curl_ctx;

    // errors ////////////////////////
    AsyncHttpResult http_result;
    U32 err_line;
    const char* err_file;
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
    AsyncResult async_result;
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

g_internal void
_curl_context_cleanup(CurlContext* curl_ctx);

template <typename T>
g_internal size_t
_libcurl_callback(void* contents, size_t size, size_t nmemb, void* userp);

template <typename T>
g_internal AsyncTaskContinuation<T>
_write_data_complete(ThreadInfo thread_info, AsyncTaskStatus<T>* task_status);

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

template <typename T>
g_internal void
_error_set(AsyncResult error_code, const char* file, S32 line, AsyncHttpTaskState<T>* ctx, String8 error_msg = {});

template <typename T>
g_internal void
_error_reset(AsyncHttpTaskState<T>* ctx);

template <typename T>
g_internal void
_curl_error_set(S32 curl_error_code, const char* file, S32 line, AsyncHttpTaskState<T>* ctx);

g_internal void
_curl_reset(CurlContext* curl_ctx);

template <typename T>
g_internal AsyncResult
_async_http_configure(Arena* arena, AsyncTaskStatus<AsyncHttpTaskState<T>>* task, HttpInfo* http_info);

template <typename T>
g_internal void
async_task_state_destroy(AsyncTaskStatus<T>* task);

template <typename T>
g_internal AsyncHttpTaskCreateResult<T>
async_http_task_run(Arena* arena, ThreadPool* thread_pool, HttpInfo* http_info, AsyncHttpTaskStateConfig<T>* config, String8 task_name);

template <typename T>
g_internal AsyncHttpTaskCreateResult<T>
async_http_task_run(ThreadPool* thread_pool, HttpInfo* http_info, AsyncHttpTaskStateConfig<T>* config, String8 task_name);

} // namespace async

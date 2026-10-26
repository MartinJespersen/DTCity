#pragma once

#include <curl/curl.h>
#include "http/http.h"
#include "async_task.hpp"
#include "async_error.hpp"
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

struct CurlContext
{
    Arena* arena;
    ChunkList<U8> chunk_list;
    CURLM* multi_handle;
    CURL* session_handle;
    curl_slist* headers;
    B32 added_to_multi;

    AsyncGenericError
    option_set(CURLoption option, curl_write_callback value,
               std::source_location location = std::source_location::current());
    AsyncGenericError
    option_set(CURLoption option, void* value,
               std::source_location location = std::source_location::current());
    AsyncGenericError
    option_set(CURLoption option, long value,
               std::source_location location = std::source_location::current());
    AsyncGenericError
    option_set_offset(CURLoption option, curl_off_t value,
               std::source_location location = std::source_location::current());
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
    AsyncGenericError error;

    void
    error_attach(const AsyncGenericError& new_error);

    // Return true on failure and attach the error to this task.
    Error
    option_set(CURLoption option, curl_write_callback value,
               std::source_location location = std::source_location::current());
    Error
    option_set(CURLoption option, void* value,
               std::source_location location = std::source_location::current());
    Error
    option_set(CURLoption option, long value,
               std::source_location location = std::source_location::current());
    Error
    option_set_offset(CURLoption option, curl_off_t value,
               std::source_location location = std::source_location::current());
};

template <typename T>
struct AsyncHttpTaskStateConfig
{
    AsyncWorkFunc<T> first_func;
    T* user_data;
    U32 max_http_retries;
    U32 max_task_retries;
    U32 timeout_sec;

    AsyncHttpTaskStateConfig(AsyncWorkFunc<T> first_func, T* user_data, U32 max_http_retries, U32 max_task_retries,
                             U32 timeout_sec = 0)
        : first_func(first_func), max_http_retries(max_http_retries), max_task_retries(max_task_retries),
          timeout_sec(timeout_sec), user_data(user_data)
    {
    }
};

template <typename T>
struct AsyncHttpTaskCreateResult
{
    AsyncGenericError async_result;
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
    AsyncHttpTaskState<T>* http_ctx;
};

HttpInfo*
http_info_create(Arena* arena, HTTP_Method http_method, String8 http_path, String8 content_type,
                 std::initializer_list<String8> additional_headers, std::initializer_list<String8> params_list);

HttpInfo*
http_info_create_get(Arena* arena, String8 http_path, std::initializer_list<String8> additional_headers = {},
                     std::initializer_list<String8> params_list = {}, String8 content_type = {});

String8
_http_content_type_header_create(Arena* arena, String8 content_type);

g_internal void
async_http_global_init();

template <typename T>
size_t
_libcurl_callback(void* contents, size_t size, size_t nmemb, void* userp);

template <typename T>
AsyncTaskContinuation<T>
_http_main(ThreadInfo thread_info, AsyncTaskStatus<T>* task_status);

template <typename T>
AsyncTaskContinuation<T>
_async_http_task_continuation(AsyncHttpTaskState<T>* http_ctx, AsyncWorkFunc<T> func, HttpInfo* http_info,
                              S64 us_delay = 0);

template <typename T>
WorkerResult
_main_thread_func(ThreadInfo thread_info, WorkerData data);

template <typename T>
B32
_async_main_thread_queue_push(ThreadPool* thread_pool, AsyncTaskStatus<T>* task_state, MainThreadWorkFunc<T> func);

CurlContext*
_curl_ctx_create(Arena* arena);

void
_curl_reset(CurlContext* curl_ctx);

void
_curl_context_cleanup(CurlContext* curl_ctx);

AsyncGenericError
async_http_configure(Arena* arena, CurlContext* curl_ctx, HttpInfo* http_info);

template <typename T>
AsyncHttpTaskCreateResult<T>
async_http_task_run(Allocator allocator, ThreadPool* thread_pool, HttpInfo* http_info, AsyncHttpTaskStateConfig<T>* config,
                    const char* task_name);

template <typename T>
AsyncHttpTaskCreateResult<T>
async_http_task_run(ThreadPool* thread_pool, HttpInfo* http_info, AsyncHttpTaskStateConfig<T>* config,
                    const char* task_name);

} // namespace async

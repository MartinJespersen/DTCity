
namespace async
{
typedef void* AsyncHandle;

struct WriteChunk
{
    WriteChunk* next;
    Buffer<U8> buffer;
};

struct WriteChunkList
{
    WriteChunk* first;
    WriteChunk* last;
    U32 buffer_byte_count;
};

template <typename T>
struct LinkedListNode
{
    LinkedListNode* next;
    T v;
};

template <typename T>
struct LinkedList
{
    LinkedListNode<T>* first;
    LinkedListNode<T>* last;
};

template <typename T>
g_internal LinkedList<T>
singly_linked_list_copy(Arena* arena, LinkedList<T>* ll)
{
    LinkedList<T> new_ll = {};
    for (LinkedListNode<T>* node = ll->first; node; node = node->next)
    {
        LinkedListNode<T>* new_node = PushStruct(arena, T);
        SLLQueuePush(new_ll.first, new_ll.last, new_node);
    }
    return new_ll;
}

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
g_internal HttpInfo<T>*
http_info_create(Arena* arena, HTTP_Method http_method, String8 http_path, String8 content_type, std::initializer_list<String8> additional_headers, std::initializer_list<String8> params_list)
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

    HttpInfo<T>* info = PushStruct(arena, HttpInfo<T>);
    info->params = params;
    info->http_path = push_str8_copy(arena, http_path);
    info->content_type = push_str8_copy(arena, content_type);
    info->http_method = http_method;
    info->headers = headers;

    return info;
}

template <typename T>
g_internal HttpInfo<T>*
http_info_create_get(Arena* arena, String8 http_path, std::initializer_list<String8> additional_headers = {}, std::initializer_list<String8> params_list = {}, String8 content_type = {})
{
    return http_info_create<T>(arena, HTTP_Method_Get, http_path, content_type, additional_headers, params_list);
}

g_internal String8
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

g_internal void
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
}

template <typename T>
g_internal size_t
_libcurl_callback(void* contents, size_t size, size_t nmemb, void* userp)
{
    size_t total_size = size * nmemb;

    AsyncTaskState<T>* async_ctx = (AsyncTaskState<T>*)userp;
    WriteChunk* chunk = PushStruct(async_ctx->arena, WriteChunk);
    chunk->buffer = BufferAlloc<U8>(async_ctx->arena, total_size);
    MemoryCopy(chunk->buffer.data, contents, total_size);

    CurlContext* curl_ctx = &async_ctx->curl_ctx;
    SLLQueuePush(curl_ctx->chunk_list.first, curl_ctx->chunk_list.last, chunk);
    curl_ctx->chunk_list.buffer_byte_count += chunk->buffer.size;

    return total_size;
}

template <typename T>
g_internal void
_write_data_complete(ThreadInfo thread_info, void* user_data);

template <typename T>
g_internal void
_async_thread_pool_push(ThreadPool* thread_pool, AsyncTaskState<T>* ctx, AsyncWorkFunc<T> func, HttpInfo<T>* http_info, S64 us_delay = 0)
{
    AsyncCallbackArg<T>* args = PushStruct(ctx->arena, AsyncCallbackArg<T>);
    args->ctx = ctx;
    args->func = func;
    args->http_info = http_info;
    WorkerTask item = {.data = args, .worker_func = _write_data_complete<T>};
    thread_pool_push(thread_pool, &item, us_delay);
}

template <typename T>
struct MainThreadTaskState
{
    AsyncTaskState<T>* task_state;
    MainThreadWorkFunc<T> main_thread_func;
};

template <typename T>
void
_main_thread_func(ThreadInfo thread_info, void* data)
{
    (void)thread_info;
    MainThreadTaskState<T>* main_thread_task_state = (MainThreadTaskState<T>*)data;

    main_thread_task_state->main_thread_func(main_thread_task_state->task_state);
}

template <typename T>
g_internal B32
_async_main_thread_queue_push(ThreadPool* thread_pool, AsyncTaskState<T>* task_state, MainThreadWorkFunc<T> func)
{
    MainThreadTaskState<T>* main_thread_task_state = PushStruct(task_state->arena, MainThreadTaskState<T>);
    main_thread_task_state->main_thread_func = func;
    main_thread_task_state->task_state = task_state;

    WorkerTask item = {.data = main_thread_task_state, .worker_func = _main_thread_func<T>};
    B32 queued = thread_pool_main_thread_queue_push(thread_pool, &item);
    return queued;
}

template <typename T>
g_internal void
_error_set(AsyncResult error_code, const char* file, S32 line, AsyncTaskState<T>* ctx, String8 error_msg = {})
{
    ctx->err_file = file;
    ctx->err_line = line;
    ctx->async_result = error_code;
    ctx->error_str = error_msg;
}

template <typename T>
g_internal void
_error_reset(AsyncTaskState<T>* ctx)
{
    ctx->async_result = AsyncResult::Success;
    ctx->error_code = 0;
    ctx->error_str = {};
    ctx->err_line = 0;
    ctx->err_file = 0;
}

template <typename T>
g_internal void
_curl_error_set(S32 curl_error_code, const char* file, S32 line, AsyncTaskState<T>* ctx)
{
    _error_set(AsyncResult::CurlError, file, line, ctx);
    ctx->error_code = curl_error_code;
}

g_internal void
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
}

template <typename T>
g_internal AsyncResult
_async_http_configure(AsyncTaskState<T>* async_ctx, HttpInfo<T>* http_info)
{
    CurlContext* curl_ctx = &async_ctx->curl_ctx;

    CURLU* url_handle = curl_url();
    defer(curl_url_cleanup(url_handle));
    if (curl_ctx->session_handle == 0 || curl_ctx->multi_handle == 0 || url_handle == 0)
    {
        return AsyncResult::HandleInitError;
    }

    CURLUcode url_err = curl_url_set(url_handle, CURLUPART_URL, (const char*)http_info->http_path.str, 0);
    if (url_err)
    {
        return AsyncResult::UrlError;
    }

    char* query_str = 0;
    if (http_info->params.node_count != 0)
    {
        for (String8Node* str_node = http_info->params.first; str_node; str_node = str_node->next)
        {
            String8 param_copy = push_str8_copy(async_ctx->arena, str_node->string);
            CURLUcode query_err = curl_url_set(url_handle, CURLUPART_QUERY, (const char*)param_copy.str, CURLU_APPENDQUERY | CURLU_URLENCODE);
            if (query_err)
            {
                return AsyncResult::QueryError;
            }
        }

        CURLUcode query_err = curl_url_get(url_handle, CURLUPART_QUERY, &query_str, 0);
        if (query_err)
        {
            return AsyncResult::QueryError;
        }
    }
    defer(if (query_str) { curl_free(query_str); });

    String8 request_url = push_str8_copy(async_ctx->arena, http_info->http_path);
    B32 append_params_to_url = http_info->http_method == HTTP_Method_Get;
    if (http_info->http_method == HTTP_Method_Post && http_info->params.node_count != 0 && !str8_match(http_info->content_type, S("application/x-www-form-urlencoded"), 0))
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
            return AsyncResult::UrlError;
        }
        request_url = push_str8_copy(async_ctx->arena, str8_c_string(url_str));
    }
    else if (http_info->http_method != HTTP_Method_Post)
    {
        return AsyncResult::InvalidMethodTypeError;
    }

    OrReturnError(curl_easy_setopt(curl_ctx->session_handle, CURLOPT_URL, (const char*)request_url.str), AsyncResult::CurlError, async_ctx, __LINE__, __FILE__);

    if (http_info->http_method == HTTP_Method_Get)
    {
        OrReturnError(curl_easy_setopt(curl_ctx->session_handle, CURLOPT_HTTPGET, 1L), AsyncResult::CurlError, async_ctx, __LINE__, __FILE__);
    }
    else if (http_info->http_method == HTTP_Method_Post)
    {
        String8 body = http_info->body;
        if (body.size == 0 && query_str != 0 && str8_match(http_info->content_type, S("application/x-www-form-urlencoded"), 0))
        {
            body = push_str8_copy(async_ctx->arena, str8_c_string(query_str));
        }

        OrReturnError(curl_easy_setopt(curl_ctx->session_handle, CURLOPT_POST, 1L), AsyncResult::CurlError, async_ctx, __LINE__, __FILE__);
        OrReturnError(curl_easy_setopt(curl_ctx->session_handle, CURLOPT_POSTFIELDS, body.str), AsyncResult::CurlError, async_ctx, __LINE__, __FILE__);
        OrReturnError(curl_easy_setopt(curl_ctx->session_handle, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)body.size), AsyncResult::CurlError, async_ctx, __LINE__, __FILE__);
    }

    curl_slist* headers = 0;
    if (http_info->content_type.size)
    {
        String8 content_type_header = _http_content_type_header_create(async_ctx->arena, http_info->content_type);
        headers = curl_slist_append(headers, (const char*)content_type_header.str);
    }
    for (String8Node* header_str = http_info->headers.first; header_str; header_str = header_str->next)
    {
        headers = curl_slist_append(headers, (const char*)header_str->string.str);
    }

    curl_ctx->headers = headers;

    OrReturnError(curl_easy_setopt(curl_ctx->session_handle, CURLOPT_HTTPHEADER, curl_ctx->headers), AsyncResult::CurlError, async_ctx, __LINE__, __FILE__);
    OrReturnError(curl_easy_setopt(curl_ctx->session_handle, CURLOPT_WRITEFUNCTION, _libcurl_callback<T>), AsyncResult::CurlError, async_ctx, __LINE__, __FILE__);
    OrReturnError(curl_easy_setopt(curl_ctx->session_handle, CURLOPT_WRITEDATA, async_ctx), AsyncResult::CurlError, async_ctx, __LINE__, __FILE__);

    CURLMcode multi_err = curl_multi_add_handle(curl_ctx->multi_handle, curl_ctx->session_handle);
    if (multi_err != CURLM_OK)
    {
        async_ctx->error_code = (U32)multi_err;
        async_ctx->err_line = __LINE__;
        async_ctx->err_file = __FILE__;
        return AsyncResult::CurlError;
    }
    curl_ctx->added_to_multi = true;

    return AsyncResult::Success;
}

template <typename T>
g_internal void
_write_data_complete(ThreadInfo thread_info, void* user_data)
{
    prof_scope_marker;

    AsyncCallbackArg<T>* arg = (AsyncCallbackArg<T>*)user_data;
    HttpInfo<T>* http_info = arg->http_info;
    AsyncTaskState<T>* async_ctx = arg->ctx;
    CurlContext* curl_ctx = &async_ctx->curl_ctx;
    defer(if (async_ctx->done.load()) { _curl_context_cleanup(&async_ctx->curl_ctx); });

    if (async_ctx->timeout_us + async_ctx->task_start_us < os_now_microseconds())
    {
        _error_set(AsyncResult::TimeoutError, __FILE__, __LINE__, async_ctx);
        async_ctx->done.store(true);
        return;
    }

    int running = 1;
    CURLMcode curl_code = curl_multi_perform(curl_ctx->multi_handle, &running);

    B32 retry = false;
    B32 http_error = false;
    if (curl_code != CURLM_OK)
    {
        _curl_error_set(curl_code, __FILE__, __LINE__, async_ctx);
        retry = true;
    }
    else if (!running && retry == false)
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
            return;

        CURLcode result = msg->data.result;
        long http_code = 0;
        curl_easy_getinfo(msg->easy_handle, CURLINFO_RESPONSE_CODE, &http_code);

        UserFuncResult<T> user_result = {};
        AsyncWorkFunc<T> next_func = {};
        if (result != CURLE_OK)
        {
            AsyncResult error_type = result ? AsyncResult::CurlError : AsyncResult::HttpError;
            S32 error_code = result ? result : http_code;
            _error_set(error_type, __FILE__, __LINE__, async_ctx);
            async_ctx->error_code = error_code;
            retry = true;
        }
        else
        {
            // prepare response body as string input
            U32 count = 0;
            String8 final_str_buffer = push_str8_fill_byte(async_ctx->arena, curl_ctx->chunk_list.buffer_byte_count, 0);
            for (WriteChunk* node = curl_ctx->chunk_list.first; node; node = node->next)
            {
                MemoryCopy(final_str_buffer.str + count, node->buffer.data, node->buffer.size);
                count += node->buffer.size;
            }

            if (http_code >= 400)
            {
                AsyncResult error_type = AsyncResult::HttpError;
                S32 error_code = http_code;
                _error_set(error_type, __FILE__, __LINE__, async_ctx, final_str_buffer);
                async_ctx->error_code = error_code;
                http_error = true;
                retry = true;
            }

            if (!http_error)
            {
                // call user function
                AsyncWorkFunc<T> func = arg->func;
                AssertAlways(func != 0);
                user_result = func(async_ctx->arena, thread_info.thread_pool, final_str_buffer, &async_ctx->task_state);
                next_func = user_result.next_func;
            }
        }

        // retry if call not succesful and max retries have not been reached.
        B32 task_retry = false;
        U64 us_delay = 0;

        if (user_result.successful)
        {
            async_ctx->cur_http_retry_count = 0;
        }
        else if (user_result.to_reschedule)
        {
            // user function retry
            retry = true;
            us_delay = user_result.us_delay;
            async_ctx->cur_http_retry_count = 0;
        }
        else if (async_ctx->cur_http_retry_count < async_ctx->max_http_retries)
        {
            // task http call retry
            async_ctx->cur_http_retry_count += 1;
            DEBUG_LOG("Task: %s: Retry Error Type: %d. Error Code: %d", async_ctx->task_name.str, async_ctx->async_result, async_ctx->error_code);
            DEBUG_LOG("Task: %s: Rescheduling http retry %d/%d...", async_ctx->task_name.str, async_ctx->cur_http_retry_count, async_ctx->max_http_retries);
            us_delay = 10'000'000; // 10 sec
            retry = true;
        }
        else if (async_ctx->cur_task_retry_count < async_ctx->max_task_retries)
        {
            // task retry
            async_ctx->cur_task_retry_count += 1;
            async_ctx->cur_http_retry_count = 0;
            task_retry = true;
            DEBUG_LOG("Task: %s: Retry Error Type: %d. Error Code: %d", async_ctx->task_name.str, async_ctx->async_result, async_ctx->error_code);
            DEBUG_LOG("Task: %s: Rescheduling task retry %d/%d...", async_ctx->task_name.str, async_ctx->cur_task_retry_count, async_ctx->max_task_retries);
            us_delay = 10'000'000; // 10 sec
            retry = true;
        }

        // reschedule with optional delay
        if (retry)
        {
            if (task_retry)
            {
                http_info = async_ctx->first_http_info;
                next_func = async_ctx->first_func;
                DEBUG_LOG("Full Task Retry...");
            }
            else
            {
                next_func = arg->func;
            }

            _error_reset(async_ctx);
            _curl_reset(&async_ctx->curl_ctx);
            AsyncResult configure_result = _async_http_configure(async_ctx, http_info);
            if (configure_result != AsyncResult::Success)
            {
                async_ctx->async_result = configure_result;
                async_ctx->done.store(true);
                return;
            }
            _async_thread_pool_push(thread_info.thread_pool, async_ctx, next_func, http_info, us_delay);
            return;
        }

        // record user function error and end the session
        if (user_result.successful == false)
        {
            async_ctx->error_str = push_str8_copy(async_ctx->arena, user_result.msg);
            async_ctx->async_result = AsyncResult::UserFunctionError;
            async_ctx->done.store(true);
            return;
        }

        HttpInfo<T>* next_http_info = user_result.http_info;
        if (next_http_info != 0)
        {
            AssertAlways(next_func != 0);
            _curl_reset(&async_ctx->curl_ctx);
            AsyncResult configure_result = _async_http_configure(async_ctx, next_http_info);
            if (configure_result != AsyncResult::Success)
            {
                async_ctx->async_result = configure_result;
                async_ctx->done.store(true);
                return;
            }
            _async_thread_pool_push(thread_info.thread_pool, async_ctx, next_func, next_http_info);
        }
        else
        {
            async_ctx->success.store(true);
            async_ctx->done.store(true);
            if (user_result.main_thread_func)
            {
                _async_main_thread_queue_push(thread_info.thread_pool, async_ctx, user_result.main_thread_func);
            }
        }
    }
    else
    {
        _async_thread_pool_push(thread_info.thread_pool, async_ctx, arg->func, arg->http_info);
    }
}

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
g_internal std::shared_ptr<AsyncTaskState<T>>
async_task_state_create(String8 name)
{
    Arena* arena = arena_alloc();
    AsyncTaskState<T>* async_ctx = PushStruct(arena, AsyncTaskState<T>);
    async_ctx->arena = arena;
    async_ctx->task_name = push_str8_copy(arena, name);
    return std::shared_ptr<AsyncTaskState<T>>(async_ctx, AsyncCallCtxDeleter<T>{});
}

template <typename T>
g_internal AsyncResult
async_http_task_create(const std::shared_ptr<AsyncTaskState<T>>& async_ctx, ThreadPool* thread_pool, HTTP_Method http_method, String8 http_path, String8 content_type, String8 body,
                       std::initializer_list<String8> params, std::initializer_list<String8> additional_headers_list, AsyncWorkFunc<T> func, U32 max_http_retry_count = 0, U32 task_retry_count = 0,
                       U32 timeout_sec = 300)
{
    async_http_global_init();
    AsyncTaskState<T>* async_ctx_ptr = async_ctx.get();
    async_ctx->timeout_us = timeout_sec * 1'000'000;
    async_ctx->task_start_us = os_now_microseconds();
    async_ctx->max_task_retries = task_retry_count;
    async_ctx->first_func = func;

    // curl library inits
    CURL* session_handle = curl_easy_init();
    CURLM* multi_handle = curl_multi_init();

    // create curl handles
    CurlContext* curl_ctx = &async_ctx_ptr->curl_ctx;
    async_ctx_ptr->max_http_retries = max_http_retry_count;
    curl_ctx->session_handle = session_handle;
    curl_ctx->multi_handle = multi_handle;
    defer(if (async_ctx_ptr->started == false) { _curl_context_cleanup(curl_ctx); });

    // thread pool push
    HttpInfo<T>* http_info = http_info_create<T>(async_ctx_ptr->arena, http_method, http_path, content_type, additional_headers_list, params);
    http_info->body = push_str8_copy(async_ctx->arena, body);
    async_ctx_ptr->first_http_info = http_info;
    AsyncResult async_result = _async_http_configure(async_ctx_ptr, http_info);
    if (async_result != AsyncResult::Success)
    {
        return async_result;
    }
    async_ctx_ptr->started = true;
    _async_thread_pool_push(thread_pool, async_ctx_ptr, async_ctx->first_func, http_info);

    return AsyncResult::Success;
}

} // namespace async

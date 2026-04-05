
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

template <typename T> struct LinkedListNode
{
    LinkedListNode* next;
    T v;
};

template <typename T> struct LinkedList
{
    LinkedListNode<T>* first;
    LinkedListNode<T>* last;
};

enum class ContentType
{
    None,
    FormUrlEncoded,
    ApplicationJson
};

struct HttpInfo
{
    String8List params;
    String8 http_path;
    ContentType content_type;
    HTTP_Method http_method;
    String8List headers;
};

struct UserFuncResult;
typedef UserFuncResult (*AsyncWorkFunc)(Arena* arena, String8);

struct UserFuncResult
{
    String8 msg;
    B32 successful;
    B32 to_reschedule;
    S64 ms_delay;
    HttpInfo* http_info;
    AsyncWorkFunc next_func;

    static UserFuncResult
    reschedule(S64 ms_delay = 0)
    {
        UserFuncResult result = {.to_reschedule = true, .ms_delay = ms_delay};
        return result;
    }

    static UserFuncResult
    failure(Arena* arena, const char* msg, ...)
    {
        va_list args;
        va_start(args, msg);
        String8 msg_final = push_str8fv(arena, msg, args);
        va_end(args);

        UserFuncResult result = {.msg = msg_final};
        return result;
    }

    static UserFuncResult
    success(HttpInfo* http_info, AsyncWorkFunc next_func = 0)
    {
        UserFuncResult result = {.successful = true, .http_info = http_info, .next_func = next_func};
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
    UserFunctionError

};

#define OrReturnError(err, async_error, ctx)                                                                                                                                                           \
    do                                                                                                                                                                                                 \
    {                                                                                                                                                                                                  \
        AsyncResult error = (AsyncResult)(async_error);                                                                                                                                                \
        if ((bool)(err))                                                                                                                                                                               \
        {                                                                                                                                                                                              \
            (ctx)->curl_error = (U32)(err);                                                                                                                                                            \
            (ctx)->err_line = __LINE__;                                                                                                                                                                \
            (ctx)->err_file = __FILE__;                                                                                                                                                                \
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

struct AsyncCallCtx
{
    Arena* arena;
    B32 started;
    std::atomic<B32> done;
    std::atomic<B32> success;
    U32 cur_retry_count;
    U32 max_retry_count;

    LinkedList<AsyncWorkFunc> funcs;

    CurlContext curl_ctx;

    // errors ////////////////////////
    AsyncResult async_result;
    U32 err_line;
    const char* err_file;
    U32 curl_error;
    U32 http_error_code;
    String8 user_error_str;
};

struct AsyncCallbackArg
{
    AsyncCallCtx* ctx;
    AsyncWorkFunc func;
    HttpInfo* http_info;
};

g_internal HttpInfo*
http_info_create(Arena* arena, HTTP_Method http_method, String8 http_path, ContentType content_type, std::initializer_list<String8> additional_headers, std::initializer_list<String8> params_list)
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
    info->content_type = content_type;
    info->http_method = http_method;
    info->headers = headers;

    return info;
}

g_internal HttpInfo*
http_info_create_get(Arena* arena, String8 http_path, ContentType content_type, std::initializer_list<String8> additional_headers = {}, std::initializer_list<String8> params_list = {})
{
    return http_info_create(arena, HTTP_Method_Get, http_path, content_type, additional_headers, params_list);
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

g_internal size_t
_libcurl_callback(void* contents, size_t size, size_t nmemb, void* userp)
{
    size_t total_size = size * nmemb;

    AsyncCallCtx* async_ctx = (AsyncCallCtx*)userp;
    WriteChunk* chunk = PushStruct(async_ctx->arena, WriteChunk);
    chunk->buffer = BufferAlloc<U8>(async_ctx->arena, total_size);
    MemoryCopy(chunk->buffer.data, contents, total_size);

    CurlContext* curl_ctx = &async_ctx->curl_ctx;
    SLLQueuePush(curl_ctx->chunk_list.first, curl_ctx->chunk_list.last, chunk);
    curl_ctx->chunk_list.buffer_byte_count += chunk->buffer.size;

    return total_size;
}

g_internal void
_write_data_complete(ThreadInfo thread_info, void* user_data);

g_internal void
_async_thread_pool_push(ThreadPool* thread_pool, AsyncCallCtx* ctx, AsyncWorkFunc func, HttpInfo* http_info, S64 ms_delay = 0)
{
    AsyncCallbackArg* args = PushStruct(ctx->arena, AsyncCallbackArg);
    args->ctx = ctx;
    args->func = func;
    args->http_info = http_info;
    WorkerTask item = {.data = args, .worker_func = _write_data_complete};
    thread_pool_push(thread_pool, &item, ms_delay);
}
#define SetAsyncError(ctx) ((ctx)->err_line = __LINE__, (ctx)->err_file = __FILE__)

g_internal void
_curl_error_set(S32 error_code, const char* file, S32 line, AsyncCallCtx* ctx)
{
    ctx->err_file = file;
    ctx->err_line = line;
    ctx->async_result = AsyncResult::CurlError;
    ctx->curl_error = error_code;
}

g_internal void
_http_error_set(S32 error_code, const char* file, S32 line, AsyncCallCtx* ctx)
{
    ctx->err_file = file;
    ctx->err_line = line;
    ctx->async_result = AsyncResult::HttpError;
    ctx->http_error_code = error_code;
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

g_internal AsyncResult
_async_http_configure(AsyncCallCtx* async_ctx, HttpInfo* http_info)
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
    if (http_info->http_method == HTTP_Method_Get)
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

    OrReturnError(curl_easy_setopt(curl_ctx->session_handle, CURLOPT_URL, (const char*)request_url.str), AsyncResult::CurlError, async_ctx);

    if (http_info->http_method == HTTP_Method_Get)
    {
        OrReturnError(curl_easy_setopt(curl_ctx->session_handle, CURLOPT_HTTPGET, 1L), AsyncResult::CurlError, async_ctx);
    }
    else if (http_info->http_method == HTTP_Method_Post)
    {
        if (http_info->content_type != ContentType::FormUrlEncoded)
        {
            return AsyncResult::InvalidContentTypeError;
        }

        String8 body = {};
        if (query_str != 0)
        {
            body = push_str8_copy(async_ctx->arena, str8_c_string(query_str));
        }

        OrReturnError(curl_easy_setopt(curl_ctx->session_handle, CURLOPT_POST, 1L), AsyncResult::CurlError, async_ctx);
        OrReturnError(curl_easy_setopt(curl_ctx->session_handle, CURLOPT_POSTFIELDS, body.str), AsyncResult::CurlError, async_ctx);
        OrReturnError(curl_easy_setopt(curl_ctx->session_handle, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)body.size), AsyncResult::CurlError, async_ctx);
    }

    curl_slist* headers = 0;
    if (http_info->content_type == ContentType::FormUrlEncoded)
    {
        headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");
    }
    else if (http_info->content_type == ContentType::ApplicationJson)
    {
        headers = curl_slist_append(headers, "Content-Type: application/json");
    }
    for (String8Node* header_str = http_info->headers.first; header_str; header_str = header_str->next)
    {
        headers = curl_slist_append(headers, (const char*)header_str->string.str);
    }

    curl_ctx->headers = headers;

    OrReturnError(curl_easy_setopt(curl_ctx->session_handle, CURLOPT_HTTPHEADER, curl_ctx->headers), AsyncResult::CurlError, async_ctx);
    OrReturnError(curl_easy_setopt(curl_ctx->session_handle, CURLOPT_WRITEFUNCTION, _libcurl_callback), AsyncResult::CurlError, async_ctx);
    OrReturnError(curl_easy_setopt(curl_ctx->session_handle, CURLOPT_WRITEDATA, async_ctx), AsyncResult::CurlError, async_ctx);

    CURLMcode multi_err = curl_multi_add_handle(curl_ctx->multi_handle, curl_ctx->session_handle);
    if (multi_err != CURLM_OK)
    {
        async_ctx->curl_error = (U32)multi_err;
        async_ctx->err_line = __LINE__;
        async_ctx->err_file = __FILE__;
        return AsyncResult::CurlError;
    }
    curl_ctx->added_to_multi = true;

    return AsyncResult::Success;
}

g_internal void
_write_data_complete(ThreadInfo thread_info, void* user_data)
{
    prof_scope_marker;

    AsyncCallbackArg* arg = (AsyncCallbackArg*)user_data;
    HttpInfo* http_info = arg->http_info;
    AsyncCallCtx* async_ctx = arg->ctx;
    CurlContext* curl_ctx = &async_ctx->curl_ctx;
    defer(if (async_ctx->done.load()) { _curl_context_cleanup(&async_ctx->curl_ctx); });

    int running = 1;
    CURLMcode curl_code = curl_multi_perform(curl_ctx->multi_handle, &running);

    if (curl_code != CURLM_OK)
    {
        _curl_error_set(curl_code, __FILE__, __LINE__, async_ctx);
        async_ctx->done.store(true);
    }
    else if (!running)
    {
        S32 msgs_left = 0;
        for (CURLMsg* msg = curl_multi_info_read(curl_ctx->multi_handle, &msgs_left); msg; msg = curl_multi_info_read(curl_ctx->multi_handle, &msgs_left))
        {
            if (msg->msg == CURLMSG_DONE && msg->easy_handle == curl_ctx->session_handle)
            {
                CURLcode result = msg->data.result;
                long http_code = 0;
                curl_easy_getinfo(msg->easy_handle, CURLINFO_RESPONSE_CODE, &http_code);

                if (result != CURLE_OK)
                {
                    _curl_error_set(result, __FILE__, __LINE__, async_ctx);
                    async_ctx->done.store(true);
                    return;
                }
                else if (http_code >= 400)
                {
                    _http_error_set(http_code, __FILE__, __LINE__, async_ctx);
                    async_ctx->done.store(true);
                    return;
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

                    // call function
                    UserFuncResult user_result = arg->func(async_ctx->arena, final_str_buffer);

                    // retry if call not succesful and max retries have not been reached
                    if (user_result.to_reschedule == false && user_result.successful == false && async_ctx->cur_retry_count < async_ctx->max_retry_count)
                    {
                        async_ctx->cur_retry_count += 1;
                        DEBUG_LOG("Rescheduling with retry %d/%d...", async_ctx->cur_retry_count, async_ctx->max_retry_count);
                        user_result.to_reschedule = true;
                        user_result.ms_delay = 1'000'000;
                    }

                    // reschedule with optional delay
                    if (user_result.to_reschedule)
                    {
                        _curl_reset(&async_ctx->curl_ctx);
                        AsyncResult configure_result = _async_http_configure(async_ctx, http_info);
                        if (configure_result != AsyncResult::Success)
                        {
                            async_ctx->async_result = configure_result;
                            async_ctx->done.store(true);
                            return;
                        }
                        _async_thread_pool_push(thread_info.thread_pool, async_ctx, arg->func, http_info, user_result.ms_delay);
                        return;
                    }

                    // record user function error and end the session
                    if (user_result.successful == false)
                    {
                        async_ctx->user_error_str = push_str8_copy(async_ctx->arena, user_result.msg);
                        async_ctx->async_result = AsyncResult::UserFunctionError;
                        async_ctx->done.store(true);
                        return;
                    }

                    AsyncWorkFunc next_func = user_result.next_func;
                    HttpInfo* next_http_info = user_result.http_info;
                    if (next_func == 0)
                    {
                        LinkedListNode<AsyncWorkFunc>* func_node = async_ctx->funcs.first;
                        if (func_node)
                        {
                            next_func = func_node->v;
                            SLLQueuePop(async_ctx->funcs.first, async_ctx->funcs.last);
                        }
                    }

                    if (next_func != 0)
                    {
                        AssertAlways(next_http_info != 0);
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
                    }
                }
                break;
            }
        }
    }
    else
    {
        _async_thread_pool_push(thread_info.thread_pool, async_ctx, arg->func, arg->http_info);
    }
}

struct AsyncCallCtxDeleter
{
    void
    operator()(AsyncCallCtx* ctx) const
    {
        // Either wait here, or assert that the caller already waited.
        AssertAlways(ctx->done.load(std::memory_order_acquire));

        _curl_context_cleanup(&ctx->curl_ctx);

        Arena* arena = ctx->arena;
        arena_release(arena);
    }
};

g_internal AsyncResult
async_http_call_create(std::shared_ptr<AsyncCallCtx>* out, ThreadPool* thread_pool, HTTP_Method http_method, String8 http_path, ContentType content_type, std::initializer_list<String8> params,
                       std::initializer_list<String8> additional_headers_list, std::initializer_list<AsyncWorkFunc> funcs, U32 retry_count = 0)
{
    async_http_global_init();

    // curl library inits
    CURL* session_handle = curl_easy_init();
    CURLM* multi_handle = curl_multi_init();

    // create curl handles
    Arena* arena = arena_alloc();
    AsyncCallCtx* async_ctx = PushStruct(arena, AsyncCallCtx);
    *out = std::shared_ptr<AsyncCallCtx>(async_ctx, AsyncCallCtxDeleter{});
    CurlContext* curl_ctx = &async_ctx->curl_ctx;
    async_ctx->arena = arena;
    async_ctx->max_retry_count = retry_count;
    curl_ctx->session_handle = session_handle;
    curl_ctx->multi_handle = multi_handle;
    defer(if (async_ctx->started == false) { _curl_context_cleanup(curl_ctx); });

    // evaluate work to be done
    for (auto func : funcs)
    {
        LinkedListNode<AsyncWorkFunc>* work = PushStruct(arena, LinkedListNode<AsyncWorkFunc>);
        work->v = func;
        SLLQueuePush(async_ctx->funcs.first, async_ctx->funcs.last, work);
    }
    LinkedListNode<AsyncWorkFunc>* work_node = async_ctx->funcs.first;
    if (!work_node)
    {
        return AsyncResult::NoWorkError;
    }

    // thread pool push
    SLLQueuePop(async_ctx->funcs.first, async_ctx->funcs.last);

    HttpInfo* http_info = http_info_create(async_ctx->arena, http_method, http_path, content_type, additional_headers_list, params);
    AsyncResult async_result = _async_http_configure(async_ctx, http_info);
    if (async_result != AsyncResult::Success)
    {
        return async_result;
    }
    async_ctx->started = true;
    _async_thread_pool_push(thread_pool, async_ctx, work_node->v, http_info);

    return AsyncResult::Success;
}

} // namespace async

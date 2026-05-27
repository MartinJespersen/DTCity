namespace async
{
g_internal HttpInfo*
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

    HttpInfo* info = PushStruct(arena, HttpInfo);
    info->params = params;
    info->http_path = push_str8_copy(arena, http_path);
    info->content_type = push_str8_copy(arena, content_type);
    info->http_method = http_method;
    info->headers = headers;

    return info;
}

g_internal HttpInfo*
http_info_create_get(Arena* arena, String8 http_path, std::initializer_list<String8> additional_headers, std::initializer_list<String8> params_list, String8 content_type)
{
    return http_info_create(arena, HTTP_Method_Get, http_path, content_type, additional_headers, params_list);
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
    LibCurlCallbackData<T>* callback_data = (LibCurlCallbackData<T>*)userp;
    AsyncHttpTaskState<T>* async_ctx = callback_data->http_ctx;

    WriteChunk* chunk = PushStruct(callback_data->arena, WriteChunk);
    chunk->buffer = buffer_alloc<U8>(callback_data->arena, total_size);
    MemoryCopy(chunk->buffer.data, contents, total_size);

    CurlContext* curl_ctx = &async_ctx->curl_ctx;
    SLLQueuePush(curl_ctx->chunk_list.first, curl_ctx->chunk_list.last, chunk);
    curl_ctx->chunk_list.buffer_byte_count += chunk->buffer.size;

    return total_size;
}

template <typename T>
g_internal AsyncTaskContinuation<T>
_async_http_task_continuation(AsyncHttpTaskState<T>* http_ctx, AsyncWorkFunc<T> func, HttpInfo* http_info, S64 us_delay)
{
    http_ctx->next_http_info = http_info;
    http_ctx->next_func = func;

    AsyncTaskContinuation<T> continuation = {};
    continuation.func = _write_data_complete<T>;
    continuation.us_delay = us_delay;
    return continuation;
}

template <typename T>
WorkerResult
_main_thread_func(ThreadInfo thread_info, WorkerData data)
{
    (void)thread_info;
    MainThreadTaskState<T>* main_thread_task_state = (MainThreadTaskState<T>*)data;

    main_thread_task_state->main_thread_func(main_thread_task_state->task_state);
    return {};
}

template <typename T>
g_internal B32
_async_main_thread_queue_push(ThreadPool* thread_pool, AsyncTaskStatus<T>* task_state, MainThreadWorkFunc<T> func)
{
    MainThreadTaskState<T>* main_thread_task_state = PushStruct(task_state->arena, MainThreadTaskState<T>);
    main_thread_task_state->main_thread_func = func;
    main_thread_task_state->task_state = task_state;

    WorkerItem item = WorkerItem(main_thread_task_state, _main_thread_func<T>);
    B32 queued = thread_pool_main_thread_queue_push(thread_pool, &item);
    return queued;
}

template <typename T>
g_internal void
_error_set(AsyncResult async_result, const char* file, S32 line, AsyncHttpTaskState<T>* ctx, String8 error_msg)
{
    ctx->err_file = file;
    ctx->err_line = line;
    ctx->http_result.async_result = async_result;
    ctx->http_result.error_str = error_msg;
}

template <typename T>
g_internal void
_error_reset(AsyncHttpTaskState<T>* ctx)
{
    ctx->http_result = {};
    ctx->err_line = 0;
    ctx->err_file = 0;
}

template <typename T>
g_internal void
_curl_error_set(S32 curl_error_code, const char* file, S32 line, AsyncHttpTaskState<T>* ctx)
{
    _error_set(AsyncResult::CurlError, file, line, ctx);
    ctx->http_result.error_code = (U32)curl_error_code;
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
_async_http_configure(Arena* arena, AsyncHttpTaskState<T>* http_ctx, HttpInfo* http_info)
{
    CurlContext* curl_ctx = &http_ctx->curl_ctx;

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
            String8 param_copy = push_str8_copy(arena, str_node->string);
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

    String8 request_url = push_str8_copy(arena, http_info->http_path);
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
        request_url = push_str8_copy(arena, str8_c_string(url_str));
    }
    else if (http_info->http_method != HTTP_Method_Post)
    {
        return AsyncResult::InvalidMethodTypeError;
    }

    OrReturnError(curl_easy_setopt(curl_ctx->session_handle, CURLOPT_URL, (const char*)request_url.str), AsyncResult::CurlError, http_ctx, __LINE__, __FILE__);

    if (http_info->http_method == HTTP_Method_Get)
    {
        OrReturnError(curl_easy_setopt(curl_ctx->session_handle, CURLOPT_HTTPGET, 1L), AsyncResult::CurlError, http_ctx, __LINE__, __FILE__);
    }
    else if (http_info->http_method == HTTP_Method_Post)
    {
        String8 body = {};
        if (http_info->body.size != 0)
        {
            body = http_info->body;
        }
        if (body.size == 0 && query_str != 0 && str8_match(http_info->content_type, S("application/x-www-form-urlencoded"), 0))
        {
            body = push_str8_copy(arena, str8_c_string(query_str));
        }

        OrReturnError(curl_easy_setopt(curl_ctx->session_handle, CURLOPT_POST, 1L), AsyncResult::CurlError, http_ctx, __LINE__, __FILE__);
        OrReturnError(curl_easy_setopt(curl_ctx->session_handle, CURLOPT_POSTFIELDS, body.str), AsyncResult::CurlError, http_ctx, __LINE__, __FILE__);
        OrReturnError(curl_easy_setopt(curl_ctx->session_handle, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)body.size), AsyncResult::CurlError, http_ctx, __LINE__, __FILE__);
    }

    curl_slist* headers = 0;
    if (http_info->content_type.size)
    {
        String8 content_type_header = _http_content_type_header_create(arena, http_info->content_type);
        headers = curl_slist_append(headers, (const char*)content_type_header.str);
    }
    for (String8Node* header_str = http_info->headers.first; header_str; header_str = header_str->next)
    {
        headers = curl_slist_append(headers, (const char*)header_str->string.str);
    }

    curl_ctx->headers = headers;

    LibCurlCallbackData<T>* callback_data = PushStruct(arena, LibCurlCallbackData<T>);
    callback_data->http_ctx = http_ctx;
    callback_data->arena = arena;

    OrReturnError(curl_easy_setopt(curl_ctx->session_handle, CURLOPT_HTTPHEADER, curl_ctx->headers), AsyncResult::CurlError, http_ctx, __LINE__, __FILE__);
    OrReturnError(curl_easy_setopt(curl_ctx->session_handle, CURLOPT_WRITEFUNCTION, _libcurl_callback<T>), AsyncResult::CurlError, http_ctx, __LINE__, __FILE__);
    OrReturnError(curl_easy_setopt(curl_ctx->session_handle, CURLOPT_WRITEDATA, callback_data), AsyncResult::CurlError, http_ctx, __LINE__, __FILE__);

    CURLMcode multi_err = curl_multi_add_handle(curl_ctx->multi_handle, curl_ctx->session_handle);
    if (multi_err != CURLM_OK)
    {
        http_ctx->http_result.error_code = (U32)multi_err;
        http_ctx->err_line = __LINE__;
        http_ctx->err_file = __FILE__;
        return AsyncResult::CurlError;
    }
    curl_ctx->added_to_multi = true;

    return AsyncResult::Success;
}

template <typename T>
g_internal AsyncTaskContinuation<T>
_write_data_complete(ThreadInfo thread_info, AsyncTaskStatus<T>* task_status)
{
    prof_scope_marker;

    AssertAlways(task_status->ext_type == async::ExtensionType::Http);
    AsyncHttpTaskState<T>* http_ctx = task_status->http_ext;
    HttpInfo* next_http_info = http_ctx->next_http_info;
    AsyncWorkFunc<T> next_func = http_ctx->next_func;
    CurlContext* curl_ctx = &http_ctx->curl_ctx;

    if (http_ctx->timeout_us + http_ctx->task_start_us < os_now_microseconds())
    {
        _error_set(AsyncResult::TimeoutError, __FILE__, __LINE__, http_ctx);
        Debug_Http_Push(http_ctx->http_result);
        task_status->error.store(true);
        return {};
    }

    int running = 1;
    CURLMcode curl_code = curl_multi_perform(curl_ctx->multi_handle, &running);

    B32 retry = false;
    B32 http_error = false;
    if (curl_code != CURLM_OK)
    {
        _curl_error_set(curl_code, __FILE__, __LINE__, http_ctx);
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
        {
            task_status->error.store(true);
            return {};
        }

        CURLcode result = msg->data.result;
        long http_code = 0;
        curl_easy_getinfo(msg->easy_handle, CURLINFO_RESPONSE_CODE, &http_code);

        UserFuncResult<T> user_result = {};
        if (result != CURLE_OK)
        {
            AsyncResult error_type = result ? AsyncResult::CurlError : AsyncResult::HttpError;
            S32 error_code = result ? result : http_code;
            _error_set(error_type, __FILE__, __LINE__, http_ctx);
            http_ctx->http_result.error_code = (U32)error_code;
            retry = true;
        }
        else
        {
            // prepare response body as string input
            U32 count = 0;
            String8 final_str_buffer = push_str8_fill_byte(task_status->arena, curl_ctx->chunk_list.buffer_byte_count, 0);
            for (WriteChunk* node = curl_ctx->chunk_list.first; node; node = node->next)
            {
                MemoryCopy(final_str_buffer.str + count, node->buffer.data, node->buffer.size);
                count += node->buffer.size;
            }

            if (http_code >= 400)
            {
                AsyncResult error_type = AsyncResult::HttpError;
                S32 error_code = http_code;
                _error_set(error_type, __FILE__, __LINE__, http_ctx, final_str_buffer);
                http_ctx->http_result.error_code = (U32)error_code;
                http_error = true;
                retry = true;
            }

            if (!http_error)
            {
                // call user function
                AssertAlways(next_func != 0);
                user_result = next_func(task_status->arena, thread_info.thread_pool, final_str_buffer, task_status->user_data);
                next_func = user_result.next_func ? user_result.next_func : next_func;
            }
        }

        // retry if call not succesful and max retries have not been reached.
        B32 task_retry = false;
        U64 us_delay = 0;

        if (user_result.successful)
        {
            http_ctx->cur_http_retry_count = 0;
        }
        else if (user_result.to_reschedule)
        {
            // user function retry
            retry = true;
            us_delay = user_result.us_delay;
            http_ctx->cur_http_retry_count = 0;
        }
        else if (http_ctx->cur_http_retry_count < http_ctx->max_http_retries)
        {
            // task http call retry
            http_ctx->cur_http_retry_count += 1;
            Debug_Http_Push(http_ctx->http_result);
            us_delay = 10'000'000; // 10 sec
            retry = true;
        }
        else if (http_ctx->cur_task_retry_count < http_ctx->max_task_retries)
        {
            // task retry
            http_ctx->cur_task_retry_count += 1;
            http_ctx->cur_http_retry_count = 0;
            task_retry = true;
            Debug_Http_Push(http_ctx->http_result);
            us_delay = 10'000'000; // 10 sec
            retry = true;
        }

        // reschedule with optional delay
        if (retry)
        {
            if (task_retry)
            {
                next_http_info = http_ctx->first_http_info;
                next_func = http_ctx->first_func;
            }

            _error_reset(http_ctx);
            _curl_reset(&http_ctx->curl_ctx);
            AsyncResult configure_result = _async_http_configure(task_status->arena, http_ctx, next_http_info);
            if (configure_result != AsyncResult::Success)
            {
                http_ctx->http_result.async_result = configure_result;
                Debug_Http_Push(http_ctx->http_result);
                task_status->error.store(true);
                return {};
            }

            AsyncTaskContinuation<T> continuation_func = _async_http_task_continuation(http_ctx, next_func, next_http_info, us_delay);
            return continuation_func;
        }

        // record user function error and end the session
        if (user_result.successful == false)
        {
            http_ctx->http_result.error_str = push_str8_copy(task_status->arena, user_result.msg);
            http_ctx->http_result.async_result = AsyncResult::UserFunctionError;
            Debug_Http_Push(http_ctx->http_result);
            task_status->error.store(true);
            return {};
        }

        if (user_result.successful && user_result.next_task.func)
        {
            AsyncTaskContinuation<T> continuation = {.func = user_result.next_task.func, .us_delay = user_result.next_task.us_delay};
            return continuation;
        }

        next_http_info = user_result.http_info;
        if (next_http_info != 0)
        {
            AssertAlways(next_func != 0);
            _curl_reset(&http_ctx->curl_ctx);
            AsyncResult configure_result = _async_http_configure(task_status->arena, http_ctx, next_http_info);
            if (configure_result != AsyncResult::Success)
            {
                http_ctx->http_result.async_result = configure_result;
                Debug_Http_Push(http_ctx->http_result);
                task_status->error.store(true);
                return {};
            }
            AsyncTaskContinuation<T> continuation_func = _async_http_task_continuation(http_ctx, next_func, next_http_info);
            return continuation_func;
        }
        else
        {
            Debug_Http_Push(http_ctx->http_result);
            if (user_result.main_thread_func)
            {
                _async_main_thread_queue_push(thread_info.thread_pool, task_status, user_result.main_thread_func);
            }
        }
    }
    else
    {
        AsyncTaskContinuation<T> continuation_func = _async_http_task_continuation(http_ctx, next_func, next_http_info);
        return continuation_func;
    }
    return {};
}

template <typename T>
g_internal void
async_task_state_destroy(AsyncTaskStatus<T>* task)
{
    AssertAlways(task->done.load(std::memory_order_acquire));
    AsyncHttpTaskState<T>* async_ctx = task->http_ext;
    _curl_context_cleanup(&async_ctx->curl_ctx);

    Arena* arena = task->arena;
    arena_release(arena);
}

template <typename T>
g_internal AsyncHttpTaskCreateResult<T>
async_http_task_run(Arena* arena, ThreadPool* thread_pool, HttpInfo* http_info, AsyncHttpTaskStateConfig<T>* config, String8 task_name)
{
    AsyncHttpTaskState<T>* http_ctx = PushStruct(arena, AsyncHttpTaskState<T>);
    http_ctx->thread_pool = thread_pool;
    http_ctx->first_func = config->first_func;
    http_ctx->next_func = http_ctx->first_func;
    http_ctx->first_http_info = http_info;
    http_ctx->next_http_info = http_ctx->first_http_info;
    http_ctx->max_http_retries = config->max_http_retries;
    http_ctx->max_task_retries = config->max_task_retries;
    http_ctx->timeout_sec = config->timeout_sec;

    AsyncHttpTaskCreateResult<T> result = {};

    if (http_ctx->thread_pool == 0 || http_ctx->first_func == 0 || http_ctx->first_http_info == 0)
    {
        http_ctx->http_result.async_result = AsyncResult::NoWorkError;
        result.async_result = AsyncResult::NoWorkError;
        return result;
    }

    async_http_global_init();
    if (http_ctx->timeout_us == 0)
    {
        U32 timeout_sec = http_ctx->timeout_sec == 0 ? 300 : http_ctx->timeout_sec;
        http_ctx->timeout_us = (U64)timeout_sec * 1'000'000;
    }
    http_ctx->task_start_us = os_now_microseconds();

    // curl library inits
    CURL* session_handle = curl_easy_init();
    CURLM* multi_handle = curl_multi_init();

    // create curl handles
    CurlContext* curl_ctx = &http_ctx->curl_ctx;
    curl_ctx->session_handle = session_handle;
    curl_ctx->multi_handle = multi_handle;
    defer(if (result.async_result != AsyncResult::Success) { _curl_context_cleanup(curl_ctx); });

    // thread pool push
    http_ctx->first_http_info = http_info;
    AsyncResult async_result = _async_http_configure(arena, http_ctx, http_info);
    if (async_result != AsyncResult::Success)
    {
        http_ctx->http_result.async_result = async_result;
        result.async_result = async_result;
        return result;
    }

    result.task_state = async::async_task_with_ext_run(arena, http_ctx->thread_pool, _write_data_complete, config->user_data, task_name, 0, ExtensionType::Http, http_ctx);

    result.async_result = AsyncResult::Success;
    return result;
}

template <typename T>
g_internal AsyncHttpTaskCreateResult<T>
async_http_task_run(ThreadPool* thread_pool, HttpInfo* http_info, AsyncHttpTaskStateConfig<T>* config, String8 task_name)
{
    Arena* task_arena = arena_alloc();
    AsyncHttpTaskCreateResult<T> result = async_http_task_run(task_arena, thread_pool, http_info, config, task_name);
    return result;
}
} // namespace async

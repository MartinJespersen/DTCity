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
    arena_release(curl_ctx->arena);
}

template <typename T>
g_internal size_t
_libcurl_callback(void* contents, size_t size, size_t nmemb, void* userp)
{
    size_t total_size = size * nmemb;
    LibCurlCallbackData<T>* callback_data = (LibCurlCallbackData<T>*)userp;
    AsyncHttpTaskState<T>* http_ctx = callback_data->http_ctx;
    CurlContext* curl_ctx = http_ctx->curl_ctx;

    U8* buffer = PushArray(curl_ctx->arena, U8, total_size);
    ChunkItem<U8>* chunk = chunk_item_from_array(curl_ctx->arena, buffer, total_size);
    MemoryCopy(buffer, contents, total_size);
    chunk_list_insert_chunk(&curl_ctx->chunk_list, chunk);

    return total_size;
}

template <typename T>
g_internal AsyncTaskContinuation<T>
_async_http_task_continuation(AsyncHttpTaskState<T>* http_ctx, AsyncWorkFunc<T> func, HttpInfo* http_info, S64 us_delay)
{
    http_ctx->next_http_info = http_info;
    http_ctx->next_func = func;

    AsyncTaskContinuation<T> continuation = {};
    continuation.func = _http_main<T>;
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

g_internal CurlContext*
_curl_ctx_create(Arena* arena)
{
    CurlContext* curl_ctx = PushStruct(arena, CurlContext);
    curl_ctx->arena = arena_alloc();
    Debug_SetName(curl_ctx->arena, "async HTTP curl arena");

    // curl library inits
    async_http_global_init();
    CURL* session_handle = curl_easy_init();
    CURLM* multi_handle = curl_multi_init();

    // create curl handles
    curl_ctx->session_handle = session_handle;
    curl_ctx->multi_handle = multi_handle;
    return curl_ctx;
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
    arena_clear(curl_ctx->arena);
}

g_internal AsyncError
_async_http_configure(Arena* arena, CurlContext* curl_ctx, HttpInfo* http_info, CurlWriteCallback curl_write_callback, void* callback_data)
{
    CURLU* url_handle = curl_url();
    defer(curl_url_cleanup(url_handle));
    if (curl_ctx->session_handle == 0 || curl_ctx->multi_handle == 0 || url_handle == 0)
    {
        return async_user_error(AsyncResult::HandleInitError);
    }

    B32 is_ws = str8_match(http_info->http_path, S("ws://"), MatchFlag_RightSideSloppy);
    B32 is_wss = str8_match(http_info->http_path, S("wss://"), MatchFlag_RightSideSloppy);
    B32 is_websocket = is_ws | is_wss;
    U32 url_flags = is_websocket ? CURLU_NON_SUPPORT_SCHEME : 0;

    CURLUcode url_err = curl_url_set(url_handle, CURLUPART_URL, (const char*)http_info->http_path.str, url_flags);
    if (url_err)
    {
        return async_curl_error(CurlCodeType::Url, url_err);
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
                return async_user_error(AsyncResult::QueryError);
            }
        }

        CURLUcode query_err = curl_url_get(url_handle, CURLUPART_QUERY, &query_str, 0);
        if (query_err)
        {
            return async_user_error(AsyncResult::QueryError);
        }
    }
    defer(if (query_str) { curl_free(query_str); });

    String8 request_url = push_str8_copy(arena, http_info->http_path);
    B32 append_params_to_url = http_info->http_method == HTTP_Method_Get;
    B32 passthrough_url = http_info->http_method == HTTP_Method_None;
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
            return async_curl_error(CurlCodeType::Url, request_url_err);
        }
        request_url = push_str8_copy(arena, str8_c_string(url_str));
    }
    else if (!passthrough_url && http_info->http_method != HTTP_Method_Post)
    {
        return async_user_error(AsyncResult::InvalidMethodTypeError);
    }

    async_return_curl_error(CurlCodeType::Url, curl_easy_setopt(curl_ctx->session_handle, CURLOPT_URL, (const char*)request_url.str));

    if (http_info->http_method == HTTP_Method_Get)
    {
        async_return_curl_error(CurlCodeType::Regular, curl_easy_setopt(curl_ctx->session_handle, CURLOPT_HTTPGET, 1L));
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

        async_return_curl_error(CurlCodeType::Regular, curl_easy_setopt(curl_ctx->session_handle, CURLOPT_POST, 1L));
        async_return_curl_error(CurlCodeType::Regular, curl_easy_setopt(curl_ctx->session_handle, CURLOPT_POSTFIELDS, body.str));
        async_return_curl_error(CurlCodeType::Regular, curl_easy_setopt(curl_ctx->session_handle, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)body.size));
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

    async_return_curl_error(CurlCodeType::Regular, curl_easy_setopt(curl_ctx->session_handle, CURLOPT_HTTPHEADER, curl_ctx->headers));
    async_return_curl_error(CurlCodeType::Regular, curl_easy_setopt(curl_ctx->session_handle, CURLOPT_WRITEFUNCTION, curl_write_callback));
    async_return_curl_error(CurlCodeType::Regular, curl_easy_setopt(curl_ctx->session_handle, CURLOPT_WRITEDATA, callback_data));

    CURLMcode multi_err = curl_multi_add_handle(curl_ctx->multi_handle, curl_ctx->session_handle);
    if (multi_err != CURLM_OK)
    {
        return async_curl_error(CurlCodeType::Multi, multi_err);
    }
    curl_ctx->added_to_multi = true;

    return async_no_error();
}

template <typename T>
g_internal AsyncError
_async_http_configure(Arena* arena, AsyncHttpTaskState<T>* http_ctx, HttpInfo* http_info)
{
    LibCurlCallbackData<T>* callback_data = PushStruct(arena, LibCurlCallbackData<T>);
    callback_data->http_ctx = http_ctx;
    callback_data->arena = arena;
    AsyncError result = _async_http_configure(arena, http_ctx->curl_ctx, http_info, _libcurl_callback<T>, callback_data);
    return result;
}

template <typename T>
g_internal AsyncTaskContinuation<T>
_http_main(ThreadInfo thread_info, AsyncTaskStatus<T>* task_status)
{
    prof_scope_marker;

    AssertAlways(task_status->ext_type == async::ExtensionType::Http);
    AsyncHttpTaskState<T>* http_ctx = task_status->http_ext;
    HttpInfo* next_http_info = http_ctx->next_http_info;
    AsyncWorkFunc<T> next_func = http_ctx->next_func;
    CurlContext* curl_ctx = http_ctx->curl_ctx;

    if (http_ctx->timeout_us + http_ctx->task_start_us < os_now_microseconds())
    {
        async_error_set(http_ctx, async_user_error(AsyncResult::TimeoutError));
        Debug_Http_Push(http_ctx->error);
        task_status->error.store(true);
        return {};
    }

    int running = 1;
    CURLMcode curl_code = curl_multi_perform(curl_ctx->multi_handle, &running);

    B32 retry = false;
    B32 http_error = false;
    if (curl_code != CURLM_OK)
    {
        async_error_set(http_ctx, async_curl_regular_error(curl_code));
        retry = true;
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
            task_status->error.store(true);
            return {};
        }

        CURLcode result = msg->data.result;
        long http_code = 0;
        curl_easy_getinfo(msg->easy_handle, CURLINFO_RESPONSE_CODE, &http_code);

        UserFuncResult<T> user_result = {};
        if (result != CURLE_OK)
        {
            S32 error_code = result ? result : http_code;
            async_error_set(http_ctx, async_curl_regular_error(error_code));
            http_ctx->error.curl_code = (U32)error_code;
            retry = true;
        }
        else
        {
            // prepare response body as string input
            String8 final_str_buffer = str8_from_chunk_list(task_status->arena, &curl_ctx->chunk_list);

            if (http_code >= 400)
            {
                http_ctx->http_error_code = (U32)http_code;
                async_error_set(http_ctx, async_user_error(AsyncResult::HttpError));
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
            Debug_Http_Push(http_ctx->error);
            us_delay = 10'000'000; // 10 sec
            retry = true;
        }
        else if (http_ctx->cur_task_retry_count < http_ctx->max_task_retries)
        {
            // task retry
            http_ctx->cur_task_retry_count += 1;
            http_ctx->cur_http_retry_count = 0;
            task_retry = true;
            Debug_Http_Push(http_ctx->error);
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

            http_ctx->error = async_no_error();
            _curl_reset(http_ctx->curl_ctx);
            AsyncError configure_result = _async_http_configure(task_status->arena, http_ctx, next_http_info);
            if (configure_result.result != AsyncResult::Success)
            {
                http_ctx->error = configure_result;
                Debug_Http_Push(http_ctx->error);
                task_status->error.store(true);
                return {};
            }

            AsyncTaskContinuation<T> continuation_func = _async_http_task_continuation(http_ctx, next_func, next_http_info, us_delay);
            return continuation_func;
        }

        // record user function error and end the session
        if (user_result.successful == false)
        {
            http_ctx->http_error_msg = push_str8_copy(task_status->arena, user_result.msg);
            http_ctx->error = async_user_error(AsyncResult::UserFunctionError);
            Debug_Http_Push(http_ctx->error);
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
            _curl_reset(http_ctx->curl_ctx);
            http_ctx->error = _async_http_configure(task_status->arena, http_ctx, next_http_info);
            if (http_ctx->error.has_error())
            {
                Debug_Http_Push(http_ctx->error);
                task_status->error.store(true);
                return {};
            }
            AsyncTaskContinuation<T> continuation_func = _async_http_task_continuation(http_ctx, next_func, next_http_info);
            return continuation_func;
        }
        else
        {
            Debug_Http_Push(http_ctx->error);
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
g_internal AsyncHttpTaskCreateResult<T>
async_http_task_run(Arena* arena, ThreadPool* thread_pool, HttpInfo* http_info, AsyncHttpTaskStateConfig<T>* config, const char* task_name)
{
    AsyncHttpTaskCreateResult<T> result = {};
    AsyncHttpTaskState<T>* http_ctx = PushStruct(arena, AsyncHttpTaskState<T>);
    http_ctx->curl_ctx = _curl_ctx_create(arena);
    defer(if (result.async_result.has_error()) { _curl_context_cleanup(http_ctx->curl_ctx); });
    http_ctx->thread_pool = thread_pool;
    http_ctx->first_func = config->first_func;
    http_ctx->next_func = http_ctx->first_func;
    http_ctx->first_http_info = http_info;
    http_ctx->next_http_info = http_ctx->first_http_info;
    http_ctx->max_http_retries = config->max_http_retries;
    http_ctx->max_task_retries = config->max_task_retries;
    http_ctx->timeout_sec = config->timeout_sec;

    if (http_ctx->thread_pool == 0 || http_ctx->first_func == 0 || http_ctx->first_http_info == 0)
    {
        result.async_result = async_user_error(AsyncResult::NoWorkError);
        return result;
    }

    if (http_ctx->timeout_us == 0)
    {
        U32 timeout_sec = http_ctx->timeout_sec == 0 ? 300 : http_ctx->timeout_sec;
        http_ctx->timeout_us = (U64)timeout_sec * 1'000'000;
    }
    http_ctx->task_start_us = os_now_microseconds();

    // thread pool push
    http_ctx->first_http_info = http_info;
    result.async_result = _async_http_configure(arena, http_ctx, http_info);
    if (result.async_result.has_error())
    {
        return result;
    }

    ExtensionType extension_type = ExtensionType::Http;
    result.task_state = async::async_task_with_ext_run(arena, http_ctx->thread_pool, _http_main, config->user_data, task_name, 0, extension_type, http_ctx);

    return result;
}

template <typename T>
g_internal AsyncHttpTaskCreateResult<T>
async_http_task_run(ThreadPool* thread_pool, HttpInfo* http_info, AsyncHttpTaskStateConfig<T>* config, const char* task_name)
{
    Arena* task_arena = arena_alloc();
    Debug_SetName(task_arena, task_name);
    AsyncHttpTaskCreateResult<T> result = async_http_task_run(task_arena, thread_pool, http_info, config, task_name);
    return result;
}

} // namespace async

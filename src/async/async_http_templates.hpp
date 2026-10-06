#pragma once

// Template implementations
namespace async
{
template <typename T>
AsyncError
_async_http_task_configure(Arena* arena, AsyncHttpTaskState<T>* http_ctx, HttpInfo* http_info);

template <typename T>
size_t
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
AsyncTaskContinuation<T>
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
B32
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
AsyncTaskContinuation<T>
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
                user_result =
                    next_func(task_status->arena, thread_info.thread_pool, final_str_buffer, task_status->user_data);
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
            AsyncError configure_result = _async_http_task_configure(task_status->arena, http_ctx, next_http_info);
            if (configure_result.result != AsyncResult::Success)
            {
                http_ctx->error = configure_result;
                Debug_Http_Push(http_ctx->error);
                task_status->error.store(true);
                return {};
            }

            AsyncTaskContinuation<T> continuation_func =
                _async_http_task_continuation(http_ctx, next_func, next_http_info, us_delay);
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
            AsyncTaskContinuation<T> continuation = {.func = user_result.next_task.func,
                                                     .us_delay = user_result.next_task.us_delay};
            return continuation;
        }

        next_http_info = user_result.http_info;
        if (next_http_info != 0)
        {
            AssertAlways(next_func != 0);
            _curl_reset(http_ctx->curl_ctx);
            http_ctx->error = _async_http_task_configure(task_status->arena, http_ctx, next_http_info);
            if (http_ctx->error.has_error())
            {
                Debug_Http_Push(http_ctx->error);
                task_status->error.store(true);
                return {};
            }
            AsyncTaskContinuation<T> continuation_func =
                _async_http_task_continuation(http_ctx, next_func, next_http_info);
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
AsyncHttpTaskCreateResult<T>
async_http_task_run(Arena* arena, ThreadPool* thread_pool, HttpInfo* http_info, AsyncHttpTaskStateConfig<T>* config,
                    const char* task_name)
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
    result.async_result = _async_http_task_configure(arena, http_ctx, http_info);
    if (result.async_result.has_error())
    {
        return result;
    }

    ExtensionType extension_type = ExtensionType::Http;
    result.task_state = async::async_task_with_ext_run(arena, http_ctx->thread_pool, _http_main, config->user_data,
                                                       task_name, 0, extension_type, http_ctx);

    return result;
}

template <typename T>
AsyncHttpTaskCreateResult<T>
async_http_task_run(ThreadPool* thread_pool, HttpInfo* http_info, AsyncHttpTaskStateConfig<T>* config,
                    const char* task_name)
{
    Arena* task_arena = arena_alloc();
    Debug_SetName(task_arena, task_name);
    AsyncHttpTaskCreateResult<T> result = async_http_task_run(task_arena, thread_pool, http_info, config, task_name);
    return result;
}

template <typename T>
AsyncError
_async_http_task_configure(Arena* arena, AsyncHttpTaskState<T>* http_ctx, HttpInfo* http_info)
{
    AsyncError error = async_http_configure(arena, http_ctx->curl_ctx, http_info);
    if (error.has_error())
    {
        return error;
    }

    // Reinstall the response callback after every curl reset, including retries and follow-up requests.
    LibCurlCallbackData<T>* callback_data = PushStruct(arena, LibCurlCallbackData<T>);
    callback_data->arena = arena;
    callback_data->http_ctx = http_ctx;
    async_return_curl_error(CurlCodeType::Regular, curl_easy_setopt(http_ctx->curl_ctx->session_handle,
                                                                    CURLOPT_WRITEFUNCTION, _libcurl_callback<T>));
    async_return_curl_error(CurlCodeType::Regular,
                            curl_easy_setopt(http_ctx->curl_ctx->session_handle, CURLOPT_WRITEDATA, callback_data));
    return async_no_error();
}
} // namespace async

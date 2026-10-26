#pragma once

#include "async_http.hpp"
#include "async_task_templates.hpp"

// Template implementations
namespace async
{
template <typename T>
void
AsyncHttpTaskState<T>::error_attach(const AsyncGenericError& new_error)
{
    this->error = new_error;
    Debug_Http_Push(this->error);
}

template <typename T>
Error
AsyncHttpTaskState<T>::option_set(CURLoption option, curl_write_callback value, std::source_location location)
{
    AsyncGenericError option_error = this->curl_ctx->option_set(option, value, location);
    if (option_error.has_error()) this->error_attach(option_error);
    return option_error.has_error();
}

template <typename T>
Error
AsyncHttpTaskState<T>::option_set(CURLoption option, void* value, std::source_location location)
{
    AsyncGenericError option_error = this->curl_ctx->option_set(option, value, location);
    if (option_error.has_error()) this->error_attach(option_error);
    return option_error.has_error();
}

template <typename T>
Error
AsyncHttpTaskState<T>::option_set(CURLoption option, long value, std::source_location location)
{
    AsyncGenericError option_error = this->curl_ctx->option_set(option, value, location);
    if (option_error.has_error()) this->error_attach(option_error);
    return option_error.has_error();
}

template <typename T>
Error
AsyncHttpTaskState<T>::option_set_offset(CURLoption option, curl_off_t value, std::source_location location)
{
    AsyncGenericError option_error = this->curl_ctx->option_set_offset(option, value, location);
    if (option_error.has_error()) this->error_attach(option_error);
    return option_error.has_error();
}

template <typename T>
void
_async_http_task_configure(Arena* arena, AsyncHttpTaskState<T>* http_ctx, HttpInfo* http_info);

template <typename T>
Error
AsyncTaskStatus<T>::http_reconfigure(HttpInfo* http_info)
{
    if (!has_flag(this->ext_type, ExtensionType::Http))
    {
        this->add_error();
        return true;
    }

    this->http_ext->error = {};
    _curl_reset(this->http_ext->curl_ctx);
    _async_http_task_configure(this->allocator.arena, this->http_ext, http_info);
    Error failed = this->http_ext->error.has_error();
    if (failed)
    {
        this->add_error();
    }
    return failed;
}

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
    MainThreadTaskState<T>* main_thread_task_state = PushStruct(task_state->allocator.arena, MainThreadTaskState<T>);
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
        task_status->add_error(AsyncGenericError(AsyncResult::TimeoutError));
        return {};
    }

    int running = 1;
    CURLMcode curl_code = curl_multi_perform(curl_ctx->multi_handle, &running);

    B32 retry = false;
    B32 http_error = false;
    if (curl_code != CURLM_OK)
    {
        task_status->add_error(AsyncGenericError(curl_code));
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
            task_status->add_error();
            return {};
        }

        CURLcode result = msg->data.result;
        long http_code = 0;
        curl_easy_getinfo(msg->easy_handle, CURLINFO_RESPONSE_CODE, &http_code);

        UserFuncResult<T> user_result = {};
        if (result != CURLE_OK)
        {
            http_ctx->error_attach(AsyncGenericError(result));
        }
        else
        {
            // prepare response body as string input
            String8 final_str_buffer = str8_from_chunk_list(task_status->allocator.arena, &curl_ctx->chunk_list);

            if (http_code >= 400)
            {
                http_ctx->error_attach(AsyncGenericError(AsyncResult::HttpError, (U32)http_code));
                http_error = true;
            }

            if (!http_error)
            {
                // call user function
                AssertAlways(next_func != 0);
                user_result =
                    next_func(task_status->allocator.arena, thread_info.thread_pool, final_str_buffer, task_status->user_data);
                next_func = user_result.next_func ? user_result.next_func : next_func;
            }
        }

        if (!user_result.successful && !user_result.to_reschedule && !http_ctx->error.has_error())
        {
            http_ctx->error_attach(AsyncGenericError(AsyncResult::UserFunctionError, user_result.msg));
        }

        // Retry only while the configured budgets allow it.
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
            us_delay = 10'000'000; // 10 sec
            retry = true;
        }
        else if (http_ctx->cur_task_retry_count < http_ctx->max_task_retries)
        {
            // task retry
            http_ctx->cur_task_retry_count += 1;
            http_ctx->cur_http_retry_count = 0;
            task_retry = true;
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

            if (task_status->http_reconfigure(next_http_info)) return {};

            AsyncTaskContinuation<T> continuation_func =
                _async_http_task_continuation(http_ctx, next_func, next_http_info, us_delay);
            return continuation_func;
        }

        // Keep the original curl, HTTP, or callback error when retries are exhausted.
        if (user_result.successful == false)
        {
            task_status->add_error();
            return {};
        }

        if (user_result.next_task.func)
        {
            return user_result.next_task;
        }

        next_http_info = user_result.http_info;
        if (next_http_info != 0)
        {
            AssertAlways(next_func != 0);
            if (task_status->http_reconfigure(next_http_info)) return {};
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
async_http_task_run(Allocator allocator, ThreadPool* thread_pool, HttpInfo* http_info, AsyncHttpTaskStateConfig<T>* config,
                    const char* task_name)
{
    AsyncHttpTaskCreateResult<T> result = {};
    Arena* arena = allocator.arena;
    AsyncHttpTaskState<T>* http_ctx = allocator.make<AsyncHttpTaskState<T>>();
    http_ctx->curl_ctx = _curl_ctx_create(arena);
    defer(if (result.async_result.has_error()) {
        _curl_context_cleanup(http_ctx->curl_ctx);
    });
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
        result.async_result = AsyncGenericError(AsyncResult::NoWorkError);
        return result;
    }

    if (http_ctx->timeout_us == 0)
    {
        U32 timeout_sec = http_ctx->timeout_sec == 0 ? 300 : http_ctx->timeout_sec;
        http_ctx->timeout_us = (U64)timeout_sec * 1'000'000;
    }
    http_ctx->task_start_us = os_now_microseconds();

    // Configure before transferring ownership to the scheduled task.
    _async_http_task_configure(arena, http_ctx, http_info);
    if (http_ctx->error.has_error())
    {
        result.async_result = http_ctx->error;
        return result;
    }

    ExtensionType extension_type = ExtensionType::Http;
    result.task_state = async::async_task_with_ext_run(std::move(allocator), http_ctx->thread_pool, _http_main, config->user_data,
                                                       task_name, 0, extension_type, http_ctx);

    return result;
}

template <typename T>
AsyncHttpTaskCreateResult<T>
async_http_task_run(ThreadPool* thread_pool, HttpInfo* http_info, AsyncHttpTaskStateConfig<T>* config,
                    const char* task_name)
{
    Allocator allocator = Allocator::create();
    Debug_SetName(allocator.arena, task_name);
    AsyncHttpTaskCreateResult<T> result = async_http_task_run(std::move(allocator), thread_pool, http_info, config, task_name);
    return result;
}

template <typename T>
void
_async_http_task_configure(Arena* arena, AsyncHttpTaskState<T>* http_ctx, HttpInfo* http_info)
{
    AsyncGenericError error = async_http_configure(arena, http_ctx->curl_ctx, http_info);
    if (error.has_error())
    {
        http_ctx->error_attach(error);
        return;
    }

    // Reinstall the response callback after every curl reset, including retries and follow-up requests.
    if (http_ctx->option_set(CURLOPT_WRITEFUNCTION, &_libcurl_callback<T>)) return;

    LibCurlCallbackData<T>* callback_data = PushStruct(arena, LibCurlCallbackData<T>);
    callback_data->http_ctx = http_ctx;
    if (http_ctx->option_set(CURLOPT_WRITEDATA, callback_data)) return;
}
} // namespace async

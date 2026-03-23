#include "curl/curl.h"

namespace async
{
typedef void* AsyncHandle;

template <typename T> struct AsyncHttpManager
{
    AsyncHandle multi_handle;
};

template <typename T> struct AsyncHttpCall
{
    AsyncHandle curl_handle;
};

template <typename T>
g_internal AsyncHttpManager<T>*
async_http_manager_create(Arena* arena)
{
    curl_global_init(CURL_GLOBAL_ALL);
    CURLM* multi_handle = curl_multi_init(); // The Async Manager

    AsyncHttpManager<T>* async_http = PushStruct(arena, AsyncHttpManager<T>);
    async_http->multi_handle = multi_handle;

    return async_http;
}

template <typename T>
g_internal void
async_http_manager_destroy(AsyncHttpManager<T>* async_http_manager)
{
    curl_global_cleanup();
}

g_internal size_t
_libcurl_callback(void* contents, size_t size, size_t nmemb, void* userp)
{
    size_t totalSize = size * nmemb;
    ((std::string*)userp)->append((char*)contents, totalSize);
    return totalSize;
}

template <typename T>
g_internal AsyncHttpCall<T>*
async_http_call_create(Arena* arena, String8 http_path)
{
    AsyncHttpCall<T>* async_call = PushStruct(arena, AsyncHttpCall<T>);

    CURL* curl_handle = curl_easy_init();
    async_call->curl_handle = curl_handle;
    curl_easy_setopt(async_call->curl_handle, CURLOPT_URL, http_path);
    curl_easy_setopt(async_call->curl_handle, CURLOPT_WRITEFUNCTION, _libcurl_callback);

    // schedule call to thread pool here that check whether result is available
    return async_call;
}

g_internal void
async_http_call_destroy()
{
}

template <typename T>
g_internal void
async_http_check_and_exec(AsyncHttpManager<T>* manager)
{
}

}; // namespace async

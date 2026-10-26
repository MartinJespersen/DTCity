#pragma once

#include <curl/curl.h>
#include <source_location>
#include <string>

namespace async
{
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
    TimeoutError,
    Expecting_Websocket,
    WebsocketDisconnected
};

struct AsyncGenericError
{
    AsyncResult result;
    U32 error_code;
    std::source_location location;
    std::string error_str;

    AsyncGenericError();
    explicit AsyncGenericError(AsyncResult result, U32 error_code = 0, String8 error_msg = {},
                               std::source_location location = std::source_location::current());
    AsyncGenericError(AsyncResult result, String8 error_msg,
                      std::source_location location = std::source_location::current());
    explicit AsyncGenericError(CURLcode code, std::source_location location = std::source_location::current());
    explicit AsyncGenericError(CURLUcode code, std::source_location location = std::source_location::current());
    explicit AsyncGenericError(CURLMcode code, std::source_location location = std::source_location::current());

    bool
    has_error() const;
};

} // namespace async

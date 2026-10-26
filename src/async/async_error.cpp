#include "base/base_inc.hpp"
#include "async_error.hpp"

namespace async
{

AsyncGenericError::AsyncGenericError()
    : result{}, error_code{}, location{}, error_str{}
{
}

AsyncGenericError::AsyncGenericError(AsyncResult result, U32 error_code, String8 error_msg,
                                     std::source_location location)
    : result(result), error_code(error_code), location(location), error_str(error_msg.size ? (const char*)error_msg.str : "", (size_t)error_msg.size)
{
}

AsyncGenericError::AsyncGenericError(AsyncResult result, String8 error_msg, std::source_location location)
    : AsyncGenericError(result, 0, error_msg, location)
{
}

AsyncGenericError::AsyncGenericError(CURLcode code, std::source_location location)
    : AsyncGenericError(code == CURLE_OK ? AsyncResult::Success : AsyncResult::CurlError,
                        (U32)code, code == CURLE_OK ? String8{} : str8_c_string(curl_easy_strerror(code)), location)
{
}

AsyncGenericError::AsyncGenericError(CURLUcode code, std::source_location location)
    : AsyncGenericError(code == CURLUE_OK ? AsyncResult::Success : AsyncResult::CurlError,
                        (U32)code, code == CURLUE_OK ? String8{} : str8_c_string(curl_url_strerror(code)), location)
{
}

AsyncGenericError::AsyncGenericError(CURLMcode code, std::source_location location)
    : AsyncGenericError(code == CURLM_OK ? AsyncResult::Success : AsyncResult::CurlError,
                        (U32)code, code == CURLM_OK ? String8{} : str8_c_string(curl_multi_strerror(code)), location)
{
}

bool
AsyncGenericError::has_error() const
{
    return this->result != AsyncResult::Success;
}

} // namespace async

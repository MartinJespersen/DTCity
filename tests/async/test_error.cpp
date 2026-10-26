#include "../test_inc.hpp"
#include "async/async_error.hpp"

TEST_CASE("async errors default to success in local and arena storage")
{
    async::AsyncGenericError error;
    CHECK_FALSE(error.has_error());
    CHECK(error.error_code == 0);
    CHECK(str8_c_string(error.location.file_name()).size == 0);
    CHECK(error.location.line() == 0);
    CHECK(error.error_str.empty());

    Arena* arena = arena_alloc();
    defer(arena_release(arena));
    auto* zero_error = std::construct_at(PushStruct(arena, async::AsyncGenericError));
    defer(std::destroy_at(zero_error));
    CHECK_FALSE(zero_error->has_error());
    CHECK(zero_error->error_code == 0);
    CHECK(zero_error->location.line() == 0);
    CHECK(str8_c_string(zero_error->location.file_name()).size == 0);
}

TEST_CASE("async errors preserve caller location and owned messages")
{
    String8 message = S("request failed");
    U32 expected_line = __LINE__ + 1;
    const async::AsyncGenericError error(async::AsyncResult::HttpError, 503, message);
    CHECK(error.has_error());
    CHECK(error.result == async::AsyncResult::HttpError);
    CHECK(error.error_code == 503);
    CHECK(error.location.line() == expected_line);
    CHECK(str8_match(str8_c_string(error.location.file_name()), str8_c_string(__FILE__), 0));
    CHECK(error.error_str == "request failed");

    expected_line = __LINE__ + 1;
    async::AsyncGenericError user_error(async::AsyncResult::UserFunctionError, message);
    CHECK(user_error.location.line() == expected_line);
    CHECK(user_error.error_code == 0);
    CHECK(user_error.error_str == "request failed");
}

TEST_CASE("async curl errors use the typed API and forward caller location")
{
    U32 easy_line = __LINE__ + 1;
    async::AsyncGenericError easy(CURLE_COULDNT_CONNECT);
    U32 url_line = __LINE__ + 1;
    async::AsyncGenericError url(CURLUE_BAD_SCHEME);
    U32 multi_line = __LINE__ + 1;
    async::AsyncGenericError multi(CURLM_BAD_HANDLE);

    CHECK(easy.result == async::AsyncResult::CurlError);
    CHECK(url.result == async::AsyncResult::CurlError);
    CHECK(multi.result == async::AsyncResult::CurlError);
    CHECK(easy.error_code == (U32)CURLE_COULDNT_CONNECT);
    CHECK(url.error_code == (U32)CURLUE_BAD_SCHEME);
    CHECK(multi.error_code == (U32)CURLM_BAD_HANDLE);
    CHECK(easy.location.line() == easy_line);
    CHECK(url.location.line() == url_line);
    CHECK(multi.location.line() == multi_line);
    CHECK(easy.error_str == curl_easy_strerror(CURLE_COULDNT_CONNECT));
    CHECK(url.error_str == curl_url_strerror(CURLUE_BAD_SCHEME));
    CHECK(multi.error_str == curl_multi_strerror(CURLM_BAD_HANDLE));

    CHECK_FALSE(async::AsyncGenericError(CURLE_OK).has_error());
    CHECK_FALSE(async::AsyncGenericError(CURLUE_OK).has_error());
    CHECK_FALSE(async::AsyncGenericError(CURLM_OK).has_error());
    easy = {};
    CHECK_FALSE(easy.has_error());
    CHECK(easy.error_code == 0);
    CHECK(easy.error_str.empty());
}

TEST_CASE("async error messages own a copy including embedded nulls")
{
    std::string message(256, 'x');
    message[20] = '\0';
    async::AsyncGenericError error(async::AsyncResult::UserFunctionError,
                                  str8((U8*)message.data(), message.size()));
    async::AsyncGenericError copy = error;
    message.assign(256, 'y');
    error = {};
    CHECK(copy.error_str.size() == 256);
    CHECK(copy.error_str[20] == '\0');
    CHECK(copy.error_str[100] == 'x');
}

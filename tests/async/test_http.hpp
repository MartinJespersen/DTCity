#pragma once

static async::UserFuncResult<U32>
_test_http_callback_failure(Arena* arena, async::ThreadPool* thread_pool, String8 body, U32* calls);

#pragma once

g_internal U64
_hash_u64_from_str8(String8 str);

B32
cache_needs_update(String8 cache_data_file, String8 cache_meta_file, U32 ttl_sec = max_U32);

void
cache_write(String8 cache_file, String8 content, String8 hash_content);

Result<String8>
cache_read(Arena* arena, String8 cache_file, String8 hash_input, U32 ttl_sec = max_U32);

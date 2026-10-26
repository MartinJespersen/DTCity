#include "../test_inc.hpp"
#include "base/cache.hpp"

TEST_CASE("cache write reports failures and supports retry")
{
    Arena* arena = arena_alloc();
    defer(arena_release(arena));
    String8 root = str8_path_from_str8_list(arena, {os_get_process_info()->binary_path, S("test-cache-write")});
    REQUIRE(os_make_directory(root));
    defer(os_delete_directory_at_path(root));
    String8 cache_file = str8_path_from_str8_list(arena, {root, S("data")});
    String8 meta_file = str8_concat(arena, cache_file, S(".meta"));

    SUBCASE("data and metadata can be read back")
    {
        defer(os_delete_file_at_path(cache_file));
        defer(os_delete_file_at_path(meta_file));
        CHECK_FALSE(cache_write(cache_file, S("payload"), S("key")));
        auto cached = cache_read(arena, cache_file, S("key"));
        CHECK_FALSE(cached.err);
        CHECK(str8_match(cached.v, S("payload"), 0));
    }
    SUBCASE("parent path is a file")
    {
        REQUIRE(os_write_data_to_file_path(cache_file, S("blocker")));
        defer(os_delete_file_at_path(cache_file));
        String8 nested = str8_path_from_str8_list(arena, {cache_file, S("child")});
        CHECK(cache_write(nested, S("payload"), S("key")));
    }
    SUBCASE("data path is a directory")
    {
        REQUIRE(os_make_directory(cache_file));
        defer(os_delete_directory_at_path(cache_file));
        CHECK(cache_write(cache_file, S("payload"), S("key")));
        CHECK_FALSE(os_file_path_exists(meta_file));
    }
    SUBCASE("metadata failure can be retried")
    {
        defer(os_delete_file_at_path(cache_file));
        {
            REQUIRE(os_make_directory(meta_file));
            defer(os_delete_directory_at_path(meta_file));
            CHECK(cache_write(cache_file, S("payload"), S("key")));
        }
        defer(os_delete_file_at_path(meta_file));
        CHECK_FALSE(cache_write(cache_file, S("replacement"), S("key")));
        auto cached = cache_read(arena, cache_file, S("key"));
        CHECK_FALSE(cached.err);
        CHECK(str8_match(cached.v, S("replacement"), 0));
    }
}

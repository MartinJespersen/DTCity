TEST_CASE("simulator uses resources beside its executable before the development tree")
{
    Arena* arena = arena_alloc();
    defer(arena_release(arena));
    OS_ProcessInfo* process_info = os_get_process_info();
    String8 original_binary_path = process_info->binary_path;
    defer(process_info->binary_path = original_binary_path);

    String8 test_root = str8_path_from_str8_list(arena, {original_binary_path, S("test-runtime-package")});
    B32 created = os_make_directory(test_root);
    REQUIRE(created);
    defer(os_delete_directory_at_path(test_root));
    process_info->binary_path = test_root;

    String8 fallback = simulator_resource_root_get(arena);
    String8 source_root = str8_c_string(DTCITY_PROJECT_ROOT);
    B32 uses_source = str8_match(fallback, source_root, 0);
    CHECK(uses_source);

    String8 simulator_dir = str8_path_from_str8_list(arena, {test_root, S("simulator")});
    created = os_make_directory(simulator_dir);
    REQUIRE(created);
    defer(os_delete_directory_at_path(simulator_dir));
    String8 fonts_dir = str8_path_from_str8_list(arena, {simulator_dir, S("fonts")});
    created = os_make_directory(fonts_dir);
    REQUIRE(created);
    defer(os_delete_directory_at_path(fonts_dir));

    String8 packaged_root = simulator_resource_root_get(arena);
    B32 uses_package = str8_match(packaged_root, test_root, 0);
    CHECK(uses_package);
}

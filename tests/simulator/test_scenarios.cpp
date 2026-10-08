#include "../test_inc.hpp"

TEST_CASE("Simulator discovers database filenames and selects the requested database")
{
    Arena* arena = arena_alloc();
    defer(arena_release(arena));
    OS_ProcessInfo* process_info = os_get_process_info();
    String8 directory = str8_path_from_str8_list(arena, {process_info->binary_path, S("test-simulator-scenarios")});
    B32 created = os_make_directory(directory);
    REQUIRE(created);
    defer(os_delete_directory_at_path(directory));

    String8 filenames[] = {S("city.day 1.sqlite"), S("town.sqlite3"), S("village.DB")};
    String8 expected_names[] = {S("city.day 1"), S("town"), S("village")};
    String8 paths[ArrayCount(filenames)] = {};
    defer(for (String8 path : paths) { os_delete_file_at_path(path); });
    for (U64 index = 0; index < ArrayCount(filenames); ++index)
    {
        paths[index] = str8_path_from_str8_list(arena, {directory, filenames[index]});
        sqlite3* db = NULL;
        int result = sqlite3_open((char*)paths[index].str, &db);
        defer(sqlite3_close(db));
        REQUIRE(result == SQLITE_OK);
        const char* schema =
            "CREATE TABLE agent_events(agent_info_rowid INTEGER PRIMARY KEY, time REAL, event_type INTEGER, "
            "agent_id TEXT, node_from INTEGER, node_to INTEGER, node_from_lon REAL, node_from_lat REAL, "
            "node_to_lon REAL, node_to_lat REAL);";
        result = sqlite3_exec(db, schema, NULL, NULL, NULL);
        REQUIRE(result == SQLITE_OK);
        String8 insert = push_str8f(arena, "INSERT INTO agent_events VALUES (1,10,1,'%llu',1,2,16,59,17,60)", index);
        result = sqlite3_exec(db, (char*)insert.str, NULL, NULL, NULL);
        REQUIRE(result == SQLITE_OK);
    }

    // Sidecar files, extensionless files, and subdirectories are not scenarios.
    String8 ignored_names[] = {S("unrelated.sqlite-wal"), S("unrelated.sqlite-shm"), S("readme.txt"), S("sqlite")};
    String8 ignored_paths[ArrayCount(ignored_names)] = {};
    defer(for (String8 path : ignored_paths) { os_delete_file_at_path(path); });
    for (U64 index = 0; index < ArrayCount(ignored_names); ++index)
    {
        ignored_paths[index] = str8_path_from_str8_list(arena, {directory, ignored_names[index]});
        B32 written = os_write_data_to_file_path(ignored_paths[index], S("ignored"));
        REQUIRE(written);
    }
    String8 subdirectory = str8_path_from_str8_list(arena, {directory, S("folder.sqlite")});
    created = os_make_directory(subdirectory);
    REQUIRE(created);
    defer(os_delete_directory_at_path(subdirectory));

    Buffer<SimulatorScenario> scenarios = simulator_scenarios_find(arena, directory);
    REQUIRE(scenarios.size == ArrayCount(filenames));
    for (U64 index = 0; index < ArrayCount(expected_names); ++index)
    {
        CAPTURE(index);
        String8 path = simulator_scenario_path_find(arena, directory, expected_names[index]);
        B32 matches = str8_match(path, paths[index], 0);
        REQUIRE(matches);
        CHECK(path.str[path.size] == 0);
        sqlite3* db = NULL;
        int result = sqlite3_open_v2((char*)path.str, &db, SQLITE_OPEN_READONLY, NULL);
        defer(sqlite3_close(db));
        REQUIRE(result == SQLITE_OK);
        city::ServerUpdate update = {.name = std::string((char*)expected_names[index].str, expected_names[index].size),
                                    .playback = 10, .period = 1};
        String8 reply = simulator_event_window_reply(arena, db, update, index);
        REQUIRE(reply.size > 0);
        yyjson_doc* doc = yyjson_read((char*)reply.str, reply.size, 0);
        defer(yyjson_doc_free(doc));
        REQUIRE(doc);
        yyjson_val* root = yyjson_doc_get_root(doc);
        yyjson_val* events = yyjson_obj_get(root, "stream");
        size_t event_count = yyjson_arr_size(events);
        REQUIRE(event_count == 1);
        yyjson_val* event = yyjson_arr_get(events, 0);
        yyjson_val* id = yyjson_obj_get(event, "id");
        const char* actual_id = yyjson_get_str(id);
        String8 expected_id = push_str8f(arena, "%llu", index);
        CHECK(std::string_view(actual_id) == std::string_view((char*)expected_id.str, expected_id.size));
    }
    String8 unknown = simulator_scenario_path_find(arena, directory, S("missing"));
    CHECK(unknown.size == 0);
    String8 traversal = simulator_scenario_path_find(arena, directory, S("../city.day 1"));
    CHECK(traversal.size == 0);

    // The metadata response exposes exactly the same names used for selection.
    String8 reply = simulator_metadata_reply_from_directory(arena, directory, 0, 20);
    REQUIRE(reply.size > 0);
    simdjson::padded_string json(std::string_view((char*)reply.str, reply.size));
    simdjson::ondemand::parser parser;
    simdjson::ondemand::document doc;
    auto error = parser.iterate(json).get(doc);
    REQUIRE(error == simdjson::SUCCESS);
    simdjson::ondemand::object object;
    error = doc.get_object().get(object);
    REQUIRE(error == simdjson::SUCCESS);
    city::SimulationMetadata metadata = {};
    error = city::simulator_metadata_from_json(object, &metadata);
    REQUIRE(error == simdjson::SUCCESS);
    REQUIRE(metadata.scenarios.size() == ArrayCount(expected_names));
    for (const city::Scenario& scenario : metadata.scenarios)
    {
        U64 matches = 0;
        for (String8 expected : expected_names)
        {
            if (scenario.name == std::string_view((char*)expected.str, expected.size)) ++matches;
        }
        CHECK(matches == 1);
    }
}

TEST_CASE("Simulator returns no scenarios for empty or missing database directories")
{
    Arena* arena = arena_alloc();
    defer(arena_release(arena));
    OS_ProcessInfo* process_info = os_get_process_info();
    String8 directory = str8_path_from_str8_list(arena, {process_info->binary_path, S("test-empty-simulator-scenarios")});
    Buffer<SimulatorScenario> scenarios = simulator_scenarios_find(arena, directory);
    CHECK(scenarios.size == 0);
    B32 created = os_make_directory(directory);
    REQUIRE(created);
    defer(os_delete_directory_at_path(directory));
    scenarios = simulator_scenarios_find(arena, directory);
    CHECK(scenarios.size == 0);
    String8 reply = simulator_metadata_reply_from_directory(arena, directory, 0, 0);
    REQUIRE(reply.size > 0);
    yyjson_doc* doc = yyjson_read((char*)reply.str, reply.size, 0);
    defer(yyjson_doc_free(doc));
    REQUIRE(doc);
    yyjson_val* root = yyjson_doc_get_root(doc);
    yyjson_val* names = yyjson_obj_get(root, "scenarios");
    size_t scenario_count = yyjson_arr_size(names);
    CHECK(scenario_count == 0);
}

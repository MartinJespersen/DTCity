#include "../test_inc.hpp"

TEST_CASE("client consumes events at or before playback and owns emitted IDs")
{
    Arena* arena = arena_alloc();
    defer(arena_release(arena));
    Buffer<city::AgentUpdate> pending = buffer_alloc<city::AgentUpdate>(arena, 3);
    pending.data[0].time = 9;
    pending.data[1].time = 10;
    pending.data[2].time = 11;
    pending.data[0].original_id = push_str8_copy(arena, S("agent"));
    U8* original_id = pending.data[0].original_id.str;
    Buffer<city::AgentUpdate> ready = city::simulator_events_ready(arena, pending, 10);
    REQUIRE(ready.size == 2);
    CHECK(ready.data[0].time == 9);
    CHECK(ready.data[1].time == 10);
    CHECK(pending.size == 1);
    MemoryZero(original_id, 5);
    bool id_owned = str8_match(ready.data[0].original_id, S("agent"), 0);
    CHECK(id_owned);
    ready = city::simulator_events_ready(arena, pending, 10);
    CHECK(ready.size == 0);
    ready = city::simulator_events_ready(arena, pending, 11);
    CHECK(ready.size == 1);
    CHECK(pending.size == 0);
}

TEST_CASE("requested event windows rebuild seek state and preserve interval boundaries")
{
    Arena* arena = arena_alloc();
    defer(arena_release(arena));
    sqlite3* db = NULL;
    int result = sqlite3_open(":memory:", &db);
    defer(sqlite3_close(db));
    REQUIRE(result == SQLITE_OK);
    const char* fixture =
        "CREATE TABLE agent_events(agent_info_rowid INTEGER PRIMARY KEY, time REAL, event_type INTEGER, "
        "agent_id TEXT, position_x REAL, position_y REAL, position_z REAL, "
        "direction_x REAL, direction_y REAL, direction_z REAL, speed REAL, node_from INTEGER, node_to INTEGER, way_id INTEGER);"
        "INSERT INTO agent_events VALUES"
        "(1,8,1,'1',100,200,300,1,0,0,5,9007199254740993,-9,1190023434),"
        "(2,9,2,'1',105,200,300,0,0,0,0,9007199254740993,-9,1190023434),"
        "(3,10,3,'1',105,200,300,1,0,0,5,9007199254740993,-9,1190023434),"
        "(4,10,4,'1',NULL,NULL,NULL,0,0,0,0,1,2,1190023434),"
        "(5,11,1,'2',100,200,300,1,0,0,5,9007199254740993,-9,1190023434),"
        "(6,12,2,'2',105,200,300,0,0,0,0,9007199254740993,-9,1190023434);";
    result = sqlite3_exec(db, fixture, NULL, NULL, NULL);
    REQUIRE(result == SQLITE_OK);
    city::ServerUpdate update = {.name = "1. Scenario", .playback = 10, .period = 2};
    const F64 seek_times[] = {10, 8, 10};
    const U64 expected_counts[] = {4, 2, 4};
    for (U32 index = 0; index < ArrayCount(seek_times); ++index)
    {
        update.playback = seek_times[index];
        String8 reply = simulator_event_window_reply(arena, db, update, 42);
        REQUIRE(reply.size > 0);
        simdjson::padded_string json(std::string_view((char*)reply.str, reply.size));
        simdjson::ondemand::parser parser;
        simdjson::ondemand::document doc;
        auto error = parser.iterate(json).get(doc);
        REQUIRE(error == simdjson::SUCCESS);
        U64 request_id = 0;
        error = doc["request_id"].get_uint64().get(request_id);
        REQUIRE(error == simdjson::SUCCESS);
        CHECK(request_id == 42);
        simdjson::ondemand::array events;
        error = doc["stream"].get_array().get(events);
        REQUIRE(error == simdjson::SUCCESS);
        U64 count = 0;
        F64 previous_time = -1;
        for (auto event : events)
        {
            F64 time = 0;
            error = event["time"].get_double().get(time);
            REQUIRE(error == simdjson::SUCCESS);
            CHECK(time >= previous_time);
            CHECK(time < update.playback + update.period);
            previous_time = time;
            ++count;
        }
        CHECK(count == expected_counts[index]);
    }
    update.name.clear();
    String8 reply = simulator_event_window_reply(arena, db, update, 43);
    REQUIRE(reply.size > 0);
    simdjson::padded_string json(std::string_view((char*)reply.str, reply.size));
    simdjson::ondemand::parser parser;
    simdjson::ondemand::document doc;
    auto error = parser.iterate(json).get(doc);
    REQUIRE(error == simdjson::SUCCESS);
    simdjson::ondemand::array events;
    error = doc["stream"].get_array().get(events);
    REQUIRE(error == simdjson::SUCCESS);
    size_t count = 99;
    error = events.count_elements().get(count);
    REQUIRE(error == simdjson::SUCCESS);
    CHECK(count == 0);
}

TEST_CASE("Seek baselines omit completed agents but retain arrivals in the future window")
{
    Arena* arena = arena_alloc();
    defer(arena_release(arena));
    sqlite3* db = NULL;
    int result = sqlite3_open(":memory:", &db);
    defer(sqlite3_close(db));
    REQUIRE(result == SQLITE_OK);
    const char* fixture =
        "CREATE TABLE agent_events(agent_info_rowid INTEGER PRIMARY KEY, time REAL, event_type INTEGER, "
        "agent_id TEXT, position_x REAL, position_y REAL, position_z REAL, "
        "direction_x REAL, direction_y REAL, direction_z REAL, speed REAL, node_from INTEGER, node_to INTEGER, way_id INTEGER);"
        "INSERT INTO agent_events VALUES"
        "(1,8,3,'1',100,200,300,1,0,0,5,9007199254740993,-9,1190023434),(2,9,4,'1',NULL,NULL,NULL,0,0,0,0,1,2,1190023434),"
        "(3,9,3,'2',100,200,300,1,0,0,5,9007199254740993,-9,1190023434),(4,11,4,'2',NULL,NULL,NULL,0,0,0,0,1,2,1190023434);";
    result = sqlite3_exec(db, fixture, NULL, NULL, NULL);
    REQUIRE(result == SQLITE_OK);
    city::ServerUpdate request = {.name = "1. Scenario", .playback = 10, .period = 2};
    String8 reply = simulator_event_window_reply(arena, db, request, 1);
    REQUIRE(reply.size > 0);
    yyjson_doc* doc = yyjson_read((char*)reply.str, reply.size, 0);
    defer(yyjson_doc_free(doc));
    REQUIRE(doc);
    yyjson_val* events = yyjson_obj_get(yyjson_doc_get_root(doc), "stream");
    REQUIRE(yyjson_arr_size(events) == 2);
    for (U64 index = 0; index < 2; ++index)
    {
        yyjson_val* event = yyjson_arr_get(events, index);
        CHECK(std::string_view(yyjson_get_str(yyjson_obj_get(event, "id"))) == "2");
        CHECK(yyjson_get_num(yyjson_obj_get(event, "time")) == (index == 0 ? 9 : 11));
        CHECK(yyjson_get_uint(yyjson_obj_get(event, "event_type")) == (index == 0 ? 3 : 4));
        CHECK(yyjson_get_num(yyjson_obj_get(event, "speed")) == (index == 0 ? 5 : 0));
        for (const char* field : {"position_x", "position_y", "position_z"})
        {
            yyjson_val* position = yyjson_obj_get(event, field);
            CHECK(yyjson_is_null(position) == (index == 1));
            CHECK(yyjson_is_num(position) == (index == 0));
        }
    }
}

TEST_CASE("Motion streams reconstruct seeks and advance agents between replies")
{
    ScratchScope scratch = ScratchScope(0, 0);
    sqlite3* db = NULL;
    int result = sqlite3_open(":memory:", &db);
    defer(sqlite3_close(db));
    REQUIRE(result == SQLITE_OK);
    const char* fixture =
        "CREATE TABLE agent_events(agent_info_rowid INTEGER PRIMARY KEY, time REAL, event_type INTEGER, "
        "agent_id TEXT, position_x REAL, position_y REAL, position_z REAL, "
        "direction_x REAL, direction_y REAL, direction_z REAL, speed REAL, node_from INTEGER, node_to INTEGER, way_id INTEGER);"
        "INSERT INTO agent_events VALUES"
        "(1,10,3,'7',4000000,3000000,2000000,1,0,0,5,9007199254740993,-9,-9223372036854775808),"
        "(2,20,2,'7',4000050,3000000,2000000,0,0,0,0,-9,9007199254740993,9223372036854775807),"
        "(3,25,4,'7',NULL,NULL,NULL,0,0,0,0,1,2,1190023434);";
    result = sqlite3_exec(db, fixture, NULL, NULL, NULL);
    REQUIRE(result == SQLITE_OK);
    city::AgentSim simulation(8, 1);
    const F64 seek_times[] = {10, 15, 10};
    for (F64 seek_time : seek_times)
    {
        simulation.agents_active.invalidate_all();
        simulation.agent_map.clear();
        city::ServerUpdate request = {.name = "scenario", .playback = seek_time, .period = 20};
        String8 reply = simulator_event_window_reply(scratch.arena, db, request, 1);
        REQUIRE(reply.size > 0);
        simdjson::padded_string json(std::string_view((char*)reply.str, reply.size));
        simdjson::ondemand::parser parser;
        simdjson::ondemand::document doc;
        auto error = parser.iterate(json).get(doc);
        REQUIRE(error == simdjson::SUCCESS);
        simdjson::ondemand::array events;
        error = doc["stream"].get_array().get(events);
        REQUIRE(error == simdjson::SUCCESS);
        Buffer<city::AgentUpdate> pending = {};
        error = city::simulator_events_from_json(scratch.arena, events, &pending);
        REQUIRE(error == simdjson::SUCCESS);
        REQUIRE(pending.size == 3);
        CHECK(pending[0].time == 10);
        city::Agent* agent = {};
        const F64 frame_times[] = {seek_time, 17.5, 20, 22, 25};
        for (F64 playback : frame_times)
        {
            Buffer<city::AgentUpdate> ready = city::simulator_events_ready(scratch.arena, pending, playback);
            for (const city::AgentUpdate& update : ready)
            {
                city::AgentSlotUpdate slot = simulation.update_slot(update);
                agent = slot.agent;
            }
            simulation.update_positions(playback);
            if (playback < 25)
            {
                REQUIRE(agent);
                CHECK(simulation.agents_active.item_in_use_count == 1);
                CHECK(agent->ecef_coord_cur.x == 4000000 + Min(playback - 10, 10) * 5);
                CHECK(agent->ecef_coord_cur.y == 3000000);
                CHECK(agent->ecef_coord_cur.z == 2000000);
                CHECK(agent->node_from_id == (playback < 20 ? 9007199254740993LL : -9));
                CHECK(agent->node_to_id == (playback < 20 ? -9 : 9007199254740993LL));
                CHECK(agent->way_id == (playback < 20 ? INT64_MIN : INT64_MAX));
            }
            else
                CHECK(simulation.agents_active.item_in_use_count == 0);
        }
        CHECK(pending.size == 0);
    }
}

TEST_CASE("Stream sends event-time ECEF motion and preserves equal-time source order")
{
    Arena* arena = arena_alloc();
    defer(arena_release(arena));
    sqlite3* db = NULL;
    int result = sqlite3_open(":memory:", &db);
    defer(sqlite3_close(db));
    REQUIRE(result == SQLITE_OK);
    const char* fixture =
        "CREATE TABLE agent_events(agent_info_rowid INTEGER PRIMARY KEY, time REAL, event_type INTEGER, "
        "agent_id TEXT, position_x REAL, position_y REAL, position_z REAL, "
        "direction_x REAL, direction_y REAL, direction_z REAL, speed REAL, node_from INTEGER, node_to INTEGER, way_id INTEGER);"
        "INSERT INTO agent_events VALUES"
        "(30,10.25,1,'2_bicycle',4312000.125,-634321.25,4671000.5,0.36,-0.48,0.8,13.375,9007199254740993,-9,1190023434),"
        "(20,10.25,2,'1',4312001.125,-634320.25,4671001.5,0,0,0,0,9007199254740993,-9,1190023434),"
        "(10,10.25,1,'1',4312000.125,-634321.25,4671000.5,0.36,-0.48,0.8,13.375,9007199254740993,-9,1190023434);";
    result = sqlite3_exec(db, fixture, NULL, NULL, NULL);
    REQUIRE(result == SQLITE_OK);
    city::ServerUpdate request = {.name = "motion", .playback = 10, .period = 1};
    const F64 seek_times[] = {10, 10.5};
    const F64 moving_values[] = {4312000.125, -634321.25, 4671000.5, 0.36, -0.48, 0.8, 13.375};
    const F64 stopped_values[] = {4312001.125, -634320.25, 4671001.5, 0, 0, 0, 0};
    const char* fields[] = {"position_x", "position_y", "position_z",
                           "direction_x", "direction_y", "direction_z", "speed"};
    for (F64 playback : seek_times)
    {
        request.playback = playback;
        String8 reply = simulator_event_window_reply(arena, db, request, 73);
        REQUIRE(reply.size > 0);
        yyjson_doc* doc = yyjson_read((char*)reply.str, reply.size, 0);
        defer(yyjson_doc_free(doc));
        REQUIRE(doc);
        yyjson_val* root = yyjson_doc_get_root(doc);
        CHECK(yyjson_get_uint(yyjson_obj_get(root, "msg_id")) == (U64)city::SimulationMessageKind::Stream);
        CHECK(yyjson_get_uint(yyjson_obj_get(root, "request_id")) == 73);
        yyjson_val* events = yyjson_obj_get(root, "stream");
        U64 expected_count = playback == 10 ? 3 : 2;
        REQUIRE(yyjson_arr_size(events) == expected_count);
        for (U64 index = 0; index < expected_count; ++index)
        {
            U64 source_index = index + (playback == 10 ? 0 : 1);
            yyjson_val* event = yyjson_arr_get(events, index);
            CHECK(std::string_view(yyjson_get_str(yyjson_obj_get(event, "id"))) == (source_index == 2 ? "2_bicycle" : "1"));
            CHECK(yyjson_get_uint(yyjson_obj_get(event, "event_type")) == (source_index == 1 ? 2 : 1));
            CHECK(yyjson_get_num(yyjson_obj_get(event, "time")) == 10.25);
            const F64* expected = source_index == 1 ? stopped_values : moving_values;
            for (U32 field = 0; field < ArrayCount(fields); ++field)
            {
                yyjson_val* value = yyjson_obj_get(event, fields[field]);
                REQUIRE(yyjson_is_num(value));
                CHECK(yyjson_get_num(value) == doctest::Approx(expected[field]).epsilon(1e-12));
            }
            CHECK(yyjson_is_int(yyjson_obj_get(event, "node_from_id")));
            CHECK(yyjson_get_sint(yyjson_obj_get(event, "node_from_id")) == 9007199254740993LL);
            CHECK(yyjson_get_sint(yyjson_obj_get(event, "node_to_id")) == -9);
            CHECK(yyjson_is_int(yyjson_obj_get(event, "way_id")));
            CHECK(yyjson_get_sint(yyjson_obj_get(event, "way_id")) == 1190023434);
            for (const char* field : {"lon_from", "lat_from", "lon_to", "lat_to"})
            {
                CHECK(yyjson_obj_get(event, field) == NULL);
            }
        }
    }
}

TEST_CASE("Stream rejects missing motion values except arrival positions")
{
    Arena* arena = arena_alloc();
    defer(arena_release(arena));
    sqlite3* db = NULL;
    int result = sqlite3_open(":memory:", &db);
    defer(sqlite3_close(db));
    REQUIRE(result == SQLITE_OK);
    const char* fixture =
        "CREATE TABLE agent_events(agent_info_rowid INTEGER PRIMARY KEY, time REAL, event_type INTEGER, "
        "agent_id TEXT, position_x REAL, position_y REAL, position_z REAL, "
        "direction_x REAL, direction_y REAL, direction_z REAL, speed REAL, node_from INTEGER, node_to INTEGER, way_id INTEGER);"
        "INSERT INTO agent_events VALUES(1,10,1,'1',100,200,300,1,0,0,5,9007199254740993,-9,1190023434);";
    result = sqlite3_exec(db, fixture, NULL, NULL, NULL);
    REQUIRE(result == SQLITE_OK);
    const char* invalid_updates[] = {
        "UPDATE agent_events SET position_x=NULL",
        "UPDATE agent_events SET position_x=100, direction_y=NULL",
        "UPDATE agent_events SET direction_y=0, speed=NULL",
        "UPDATE agent_events SET event_type=4, speed=NULL",
        "UPDATE agent_events SET speed=0, direction_z=NULL",
        "UPDATE agent_events SET direction_z=0, node_from=NULL",
        "UPDATE agent_events SET node_from='west'",
        "UPDATE agent_events SET node_from=1.5",
        "UPDATE agent_events SET event_type=1, node_from=NULL, node_to=NULL",
        "UPDATE agent_events SET node_from=1, node_to=2, way_id='way'",
        "UPDATE agent_events SET way_id=1.5",
        "UPDATE agent_events SET way_id=NULL",
        "UPDATE agent_events SET event_type=4, way_id=NULL",
        "UPDATE agent_events SET way_id=1, node_to=NULL",
    };
    city::ServerUpdate request = {.name = "motion", .playback = 10, .period = 1};
    for (const char* sql : invalid_updates)
    {
        CAPTURE(sql);
        result = sqlite3_exec(db, sql, NULL, NULL, NULL);
        REQUIRE(result == SQLITE_OK);
        String8 reply = simulator_event_window_reply(arena, db, request, 1);
        CHECK(reply.size == 0);
    }
}

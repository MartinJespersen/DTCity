#include "../test_inc.hpp"

TEST_CASE("client only consumes events strictly before playback and owns emitted IDs")
{
    Arena* arena = arena_alloc();
    defer(arena_release(arena));
    Buffer<city::AgentUpdate> pending = buffer_alloc<city::AgentUpdate>(arena, 3);
    pending.data[0].time = 9;
    pending.data[1].time = 10;
    pending.data[2].time = 11;
    pending.data[0].original_id = push_str8_copy(arena, S("agent"));
    U8* original_id = pending.data[0].original_id.str;
    Buffer<city::AgentUpdate> ready = city::simulator_events_before_playback(arena, pending, 10);
    REQUIRE(ready.size == 1);
    CHECK(ready.data[0].time == 9);
    CHECK(pending.size == 2);
    MemoryZero(original_id, 5);
    bool id_owned = str8_match(ready.data[0].original_id, S("agent"), 0);
    CHECK(id_owned);
    ready = city::simulator_events_before_playback(arena, pending, 10);
    CHECK(ready.size == 0);
    ready = city::simulator_events_before_playback(arena, pending, 12);
    CHECK(ready.size == 2);
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
        "agent_id TEXT, node_from INTEGER, node_to INTEGER, node_from_lon REAL, node_from_lat REAL, "
        "node_to_lon REAL, node_to_lat REAL);"
        "INSERT INTO agent_events VALUES"
        "(1,8,1,'1',1,2,16,59,17,60),"
        "(2,9,2,'1',1,2,16,59,17,60),"
        "(3,10,3,'1',1,2,16,59,17,60),"
        "(4,10,4,'1',1,2,16,59,17,60),"
        "(5,11,5,'2',1,2,16,59,17,60),"
        "(6,12,6,'2',1,2,16,59,17,60);";
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
        "agent_id TEXT, node_from INTEGER, node_to INTEGER, node_from_lon REAL, node_from_lat REAL, "
        "node_to_lon REAL, node_to_lat REAL);"
        "INSERT INTO agent_events VALUES"
        "(1,8,3,'1',1,2,16,59,17,60),(2,9,4,'1',1,2,16,59,17,60),"
        "(3,9,3,'2',1,2,16,59,17,60),(4,11,4,'2',1,2,16,59,17,60);";
    result = sqlite3_exec(db, fixture, NULL, NULL, NULL);
    REQUIRE(result == SQLITE_OK);
    city::ServerUpdate request = {.name = "1. Scenario", .playback = 10, .period = 2};
    String8 reply = simulator_event_window_reply(arena, db, request, 1);
    REQUIRE(reply.size > 0);
    simdjson::padded_string json(std::string_view((char*)reply.str, reply.size));
    simdjson::ondemand::parser parser;
    simdjson::ondemand::document doc;
    auto error = parser.iterate(json).get(doc);
    REQUIRE(error == simdjson::SUCCESS);
    simdjson::ondemand::array events;
    error = doc["stream"].get_array().get(events);
    REQUIRE(error == simdjson::SUCCESS);
    U64 count = 0;
    for (auto event : events)
    {
        city::CoordinateView view = {};
        error = event.get<city::CoordinateView>(view);
        REQUIRE(error == simdjson::SUCCESS);
        CHECK(view.id == "2");
        CHECK(view.time == (count == 0 ? 9 : 11));
        CHECK(view.event_type == (count == 0 ? 3 : 4));
        ++count;
    }
    CHECK(count == 2);
}

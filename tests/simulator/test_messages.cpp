#include "../test_inc.hpp"

TEST_CASE("Server updates serialize their message kind, name, and playback")
{
    ScratchScope scratch = ScratchScope(0, 0);
    city::ServerUpdate update = {};
    update.playback = 12345.678901234567;
    update.period = 10.125;

    SUBCASE("Zero initialization")
    {
        update.playback = 0;
        update.period = 0;
    }
    SUBCASE("Escaped name and UTF-8")
    {
        update.name = "Scenario \"A\"\\\n\t";
        for (U8 byte = 0; byte < 0x20; ++byte)
        {
            update.name.push_back((char)byte);
        }
        update.name += "\xc3\xb8";
    }

    String8 message = city::simulator_server_update_to_json(scratch.arena, update);
    std::string_view input((const char*)message.str, message.size);
    simdjson::padded_string json(input);
    simdjson::ondemand::parser parser;
    simdjson::ondemand::document doc;
    auto error = parser.iterate(json).get(doc);
    REQUIRE(error == simdjson::SUCCESS);
    U64 msg_id = 0;
    error = doc["msg_id"].get_uint64().get(msg_id);
    REQUIRE(error == simdjson::SUCCESS);
    CHECK(msg_id == (U64)city::SimulationMessageKind::ServerUpdate);
    std::string_view name;
    error = doc["name"].get_string().get(name);
    REQUIRE(error == simdjson::SUCCESS);
    CHECK(name == update.name);
    F64 playback = 0;
    error = doc["playback"].get_double().get(playback);
    REQUIRE(error == simdjson::SUCCESS);
    CHECK(playback == update.playback);
    F64 period = 0;
    error = doc["period"].get_double().get(period);
    REQUIRE(error == simdjson::SUCCESS);
    CHECK(period == update.period);
    city::ServerUpdate parsed = {};
    U64 request_id = 99;
    error = city::simulator_server_update_from_json(doc, &parsed, &request_id);
    REQUIRE(error == simdjson::SUCCESS);
    CHECK(parsed.name == update.name);
    CHECK(parsed.playback == update.playback);
    CHECK(parsed.period == update.period);
    CHECK(request_id == 0);
}

TEST_CASE("Simulator messages join directly into string-owned queue storage")
{
    std::vector<std::string> queue;

    {
        ScratchScope scratch = ScratchScope(0, 0);
        String8List parts = {};
        String8 first = push_str8_copy(scratch.arena, S("\"msg_id\":2"));
        String8 second = push_str8_copy(scratch.arena, S("\"scenario_idx\":7"));
        str8_list_push(scratch.arena, &parts, first);
        str8_list_push(scratch.arena, &parts, second);
        StringJoin join = {.pre = S("{"), .sep = S(","), .post = S("}")};
        U64 arena_position = arena_pos(scratch.arena);

        city::_simulator_message_push(&queue, &parts, &join);

        U64 joined_position = arena_pos(scratch.arena);
        CHECK(joined_position == arena_position);
        // Changing the source must not change the queued message.
        MemoryZero(first.str, first.size);
        MemoryZero(second.str, second.size);
    }

    REQUIRE(queue.size() == 1);
    CHECK(queue[0] == "{\"msg_id\":2,\"scenario_idx\":7}");
    CHECK(queue[0].c_str()[queue[0].size()] == 0);
}

TEST_CASE("Simulator message joins handle empty lists, empty parts, and multi-byte separators")
{
    std::vector<std::string> queue;
    ScratchScope scratch = ScratchScope(0, 0);
    String8List parts = {};
    StringJoin join = {.pre = S("["), .sep = S("::"), .post = S("]")};
    String8 expected = {};

    SUBCASE("Empty list retains prefix and suffix")
    {
        expected = S("[]");
    }
    SUBCASE("Empty list without delimiters produces a terminated empty message")
    {
        join = {};
        expected = S("");
    }
    SUBCASE("Single part has no separator")
    {
        str8_list_push(scratch.arena, &parts, S("one"));
        expected = S("[one]");
    }
    SUBCASE("Empty parts retain their separators")
    {
        str8_list_push(scratch.arena, &parts, S(""));
        str8_list_push(scratch.arena, &parts, S("two"));
        str8_list_push(scratch.arena, &parts, S(""));
        expected = S("[::two::]");
    }
    city::_simulator_message_push(&queue, &parts, &join);
    REQUIRE(queue.size() == 1);
    CHECK(queue[0] == std::string_view((char*)expected.str, expected.size));
    CHECK(queue[0].c_str()[queue[0].size()] == 0);
}

TEST_CASE("Simulator message queue preserves binary payloads and can be cleared and reused")
{
    std::vector<std::string> queue;
    U8 bytes[] = {'a', 0, 'b'};
    String8 binary = str8(bytes, sizeof(bytes));
    city::_simulator_message_push(&queue, binary);
    String8List parts = {};
    String8Node part = {};
    str8_list_push_node_set_string(&parts, &part, S("last"));
    StringJoin join = {};
    city::_simulator_message_push(&queue, &parts, &join);

    REQUIRE(queue.size() == 2);
    CHECK(queue[0] == std::string((char*)bytes, sizeof(bytes)));
    CHECK(queue[0].c_str()[queue[0].size()] == 0);
    CHECK(queue[1] == "last");
    MemoryZero(bytes, sizeof(bytes));
    CHECK(queue[0] == std::string("a\0b", 3));

    queue.clear();
    CHECK(queue.empty());
    queue.clear();
    city::_simulator_message_push(&queue, S("reused"));
    REQUIRE(queue.size() == 1);
    CHECK(queue[0] == "reused");
}

TEST_CASE("Simulator messages retain ownership when the queue grows")
{
    std::vector<std::string> queue;
    city::_simulator_message_push(&queue, S("short"));
    city::_simulator_message_push(&queue, S("A long message that exceeds the inline storage of std::string"));
    U64 initial_capacity = queue.capacity();
    for (U64 idx = 0; idx <= initial_capacity; ++idx)
    {
        city::_simulator_message_push(&queue, S("next"));
    }
    CHECK(queue.capacity() > initial_capacity);
    CHECK(queue[0] == "short");
    CHECK(queue[1] == "A long message that exceeds the inline storage of std::string");
    for (U64 idx = 2; idx < queue.size(); ++idx)
    {
        CHECK(queue[idx] == "next");
    }
}

TEST_CASE("Playback stops at the final timestamp and releases its event")
{
    Arena* arena = arena_alloc();
    defer(arena_release(arena));
    city::AgentUpdate final_event = {.time = 84925, .event_type = (S64)city::AgentEventType::Arrival};
    Buffer<city::AgentUpdate> pending = {.data = &final_event, .size = 1};
    F64 playback = city::simulator_playback_advance(84924.99, 0.02, 84925);
    CHECK(playback == final_event.time);
    Buffer<city::AgentUpdate> ready = city::simulator_events_ready(arena, pending, playback);
    REQUIRE(ready.size == 1);
    CHECK(ready.data[0].event_type == (S64)city::AgentEventType::Arrival);
    CHECK(pending.size == 0);
    F64 stopped = city::simulator_playback_advance(playback, 1, 84925);
    CHECK(stopped == playback);
    F64 advancing = city::simulator_playback_advance(10, 0.1, 20);
    CHECK(advancing == doctest::Approx(10.1));
}

TEST_CASE("Server updates reject missing fields and negative periods without replacing state")
{
    const char* invalid[] = {
        R"({"name":"scenario","playback":1,"request_id":1})",
        R"({"name":"scenario","playback":1,"period":-1,"request_id":1})",
        R"({"name":"scenario","playback":"bad","period":1,"request_id":1})",
        R"({"name":"scenario","playback":1,"period":1})"
    };
    for (const char* input : invalid)
    {
        std::string_view input_view(input);
        simdjson::padded_string json(input_view);
        simdjson::ondemand::parser parser;
        simdjson::ondemand::document doc;
        auto error = parser.iterate(json).get(doc);
        REQUIRE(error == simdjson::SUCCESS);
        city::ServerUpdate update = {.name = "unchanged", .playback = 4, .period = 2};
        U64 request_id = 7;
        error = city::simulator_server_update_from_json(doc, &update, &request_id);
        CHECK(error != simdjson::SUCCESS);
        CHECK(update.name == "unchanged");
        CHECK(update.playback == 4);
        CHECK(update.period == 2);
        CHECK(request_id == 7);
    }
}

TEST_CASE("Motion parsing preserves precision and owns IDs after the JSON is released")
{
    ScratchScope scratch = ScratchScope(0, 0);
    Buffer<city::AgentUpdate> events = {};
    {
        simdjson::padded_string json(std::string_view(R"([
            {"id":"22180_bicycle","time":22765.125,"event_type":1,
             "position_x":4000000.125,"position_y":3000000.25,"position_z":2000000.5,
             "direction_x":0.6,"direction_y":0.8,"direction_z":0,"speed":12.5},
            {"id":"22180_bicycle","time":22765.125,"event_type":2,
             "position_x":4000010.125,"position_y":3000020.25,"position_z":2000030.5,
             "direction_x":0,"direction_y":0,"direction_z":0,"speed":0},
            {"id":"22180_bicycle","time":22766,"event_type":4,
             "position_x":null,"position_y":null,"position_z":null,
             "direction_x":0,"direction_y":0,"direction_z":0,"speed":0}
        ])"));
        simdjson::ondemand::parser parser;
        simdjson::ondemand::document doc;
        auto error = parser.iterate(json).get(doc);
        REQUIRE(error == simdjson::SUCCESS);
        simdjson::ondemand::array array;
        error = doc.get_array().get(array);
        REQUIRE(error == simdjson::SUCCESS);
        error = city::simulator_events_from_json(scratch.arena, array, &events);
        REQUIRE(error == simdjson::SUCCESS);
        MemoryZero(json.data(), json.size());
    }
    REQUIRE(events.size == 3);
    CHECK(events[0].id == 22180);
    CHECK(events[0].vehicle_type == city::AgentType::Bicycle);
    CHECK(events[0].time == 22765.125);
    CHECK(events[0].position.x == 4000000.125);
    CHECK(events[0].position.y == 3000000.25);
    CHECK(events[0].position.z == 2000000.5);
    CHECK(events[0].direction.x == 0.6);
    CHECK(events[0].direction.y == 0.8);
    CHECK(events[0].direction.z == 0);
    CHECK(events[0].speed == 12.5);
    CHECK(events[1].event_type == 2);
    CHECK(events[2].event_type == (S64)city::AgentEventType::Arrival);
    for (const city::AgentUpdate& event : events)
    {
        bool owned = str8_match(event.original_id, S("22180_bicycle"), 0);
        CHECK(owned);
    }

    city::AgentSim simulation(8, 1);
    Buffer<city::AgentUpdate> ready = city::simulator_events_ready(scratch.arena, events, 22765.125);
    REQUIRE(ready.size == 2);
    city::AgentSlotUpdate slot = {};
    for (const city::AgentUpdate& event : ready)
        slot = simulation.update_slot(event);
    simulation.update_positions(22765.5);
    REQUIRE(slot.active);
    // Equal-time exit wins over entry, so a zero-duration trip does not drift.
    CHECK(slot.agent->ecef_coord_cur.x == 4000010.125);
    CHECK(slot.agent->ecef_coord_cur.y == 3000020.25);
    CHECK(slot.agent->ecef_coord_cur.z == 2000030.5);
    ready = city::simulator_events_ready(scratch.arena, events, 22766);
    REQUIRE(ready.size == 1);
    simulation.update_slot(ready[0]);
    CHECK(simulation.agents_active.item_in_use_count == 0);
}

TEST_CASE("Motion parsing rejects incomplete active events without replacing the output")
{
    ScratchScope scratch = ScratchScope(0, 0);
    const char* input = R"([{"id":"7","time":10,"event_type":1,
        "position_x":4000000,"position_y":3000000,"position_z":2000000,
        "direction_x":1,"direction_y":0,"direction_z":0,"speed":5}])";
    for (const char* field : {"position_x", "position_y", "position_z", "direction_x", "direction_y", "direction_z", "speed"})
    {
        for (bool null_value : {false, true})
        {
            CAPTURE(field);
            CAPTURE(null_value);
            std::string invalid(input);
            std::string key = std::string("\"") + field + "\":";
            size_t key_start = invalid.find(key);
            REQUIRE(key_start != std::string::npos);
            if (null_value)
            {
                size_t value_start = key_start + key.size();
                size_t value_end = invalid.find_first_of(",}", value_start);
                invalid.replace(value_start, value_end - value_start, "null");
            }
            else
                invalid.replace(key_start + 1, std::strlen(field), "missing_field");
            simdjson::padded_string json(invalid);
            simdjson::ondemand::parser parser;
            simdjson::ondemand::document doc;
            auto error = parser.iterate(json).get(doc);
            REQUIRE(error == simdjson::SUCCESS);
            simdjson::ondemand::array array;
            error = doc.get_array().get(array);
            REQUIRE(error == simdjson::SUCCESS);
            city::AgentUpdate original = {.id = 99};
            Buffer<city::AgentUpdate> events = {.data = &original, .size = 1};
            error = city::simulator_events_from_json(scratch.arena, array, &events);
            CHECK(error != simdjson::SUCCESS);
            CHECK(events.data == &original);
            CHECK(events.size == 1);
            CHECK(events[0].id == 99);
        }
    }
}

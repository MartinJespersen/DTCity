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

TEST_CASE("Playback passes the final timestamp so its event is released")
{
    Arena* arena = arena_alloc();
    defer(arena_release(arena));
    city::AgentUpdate final_event = {.time = 84925, .event_type = (S64)city::AgentEventType::Arrival};
    Buffer<city::AgentUpdate> pending = {.data = &final_event, .size = 1};
    F64 playback = city::simulator_playback_advance(84924.99, 0.02, 84925);
    CHECK(playback > final_event.time);
    Buffer<city::AgentUpdate> ready = city::simulator_events_before_playback(arena, pending, playback);
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

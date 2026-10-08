#include "../test_inc.hpp"

TEST_CASE("Simulation metadata owns scenario names independently of the JSON input")
{
    city::SimulationMetadata metadata = {};
    {
        simdjson::ondemand::parser parser;
        std::string input = R"({"scenarios":["city","escaped\nname",""],"msg_id":)";
        input += std::to_string((U32)city::SimulationMessageKind::MetadataRequest);
        input += R"(,"timestamp_end":42.5,"timestamp_start":-1})";
        simdjson::padded_string json(input);
        simdjson::ondemand::document doc;
        auto error = parser.iterate(json).get(doc);
        REQUIRE(error == simdjson::SUCCESS);
        simdjson::ondemand::object object;
        error = doc.get_object().get(object);
        REQUIRE(error == simdjson::SUCCESS);
        // The simulator dispatches on msg_id before passing the same object to the parser.
        U64 msg_id = 0;
        error = object["msg_id"].get_uint64().get(msg_id);
        REQUIRE(error == simdjson::SUCCESS);
        CHECK(msg_id == (U64)city::SimulationMessageKind::MetadataRequest);
        error = city::simulator_metadata_from_json(object, &metadata);
        REQUIRE(error == simdjson::SUCCESS);
    }
    REQUIRE(metadata.scenarios.size() == 3);
    const char* expected[] = {"city", "escaped\nname", ""};
    U32 index = 0;
    for (const city::Scenario& scenario : metadata.scenarios)
    {
        REQUIRE(index < 3);
        CHECK(scenario.name == expected[index]);
        CHECK(scenario.name.c_str()[scenario.name.size()] == 0);
        CHECK(scenario.timestamp_start == -1);
        CHECK(scenario.timestamp_end == 42.5);
        ++index;
    }
    city::SimulationMetadata copy = metadata;
    metadata.scenarios.clear();
    CHECK(copy.scenarios[0].name == "city");
}

TEST_CASE("Simulation metadata accepts an empty scenario array")
{
    simdjson::ondemand::parser parser;
    std::string_view input = R"({"timestamp_start":0,"timestamp_end":0,"scenarios":[]})";
    simdjson::padded_string json(input);
    simdjson::ondemand::document doc;
    auto error = parser.iterate(json).get(doc);
    REQUIRE(error == simdjson::SUCCESS);
    simdjson::ondemand::object object;
    error = doc.get_object().get(object);
    REQUIRE(error == simdjson::SUCCESS);
    city::SimulationMetadata metadata = {};
    metadata.scenarios.push_back(city::Scenario{.name = "old", .timestamp_start = 99});
    error = city::simulator_metadata_from_json(object, &metadata);
    REQUIRE(error == simdjson::SUCCESS);
    CHECK(metadata.scenarios.empty());
}

TEST_CASE("Simulation metadata rejects missing fields and incorrect types without replacing output")
{
    const char* invalid[] = {
        R"({"timestamp_end":1,"scenarios":[]})",
        R"({"timestamp_start":0,"scenarios":[]})",
        R"({"timestamp_start":0,"timestamp_end":1})",
        R"({"timestamp_start":"bad","timestamp_end":1,"scenarios":[]})",
        R"({"timestamp_start":0,"timestamp_end":null,"scenarios":[]})",
        R"({"timestamp_start":0,"timestamp_end":1,"scenarios":{}})",
        R"({"timestamp_start":0,"timestamp_end":1,"scenarios":["valid",7]})",
    };
    for (const char* input : invalid)
    {
        CAPTURE(input);
        city::SimulationMetadata metadata = {};
        metadata.scenarios.push_back(city::Scenario{.name = "unchanged", .timestamp_start = 10, .timestamp_end = 20});
        const city::Scenario* previous = metadata.scenarios.data();
        simdjson::ondemand::parser parser;
        simdjson::padded_string json = simdjson::padded_string(std::string_view(input));
        simdjson::ondemand::document doc;
        auto error = parser.iterate(json).get(doc);
        REQUIRE(error == simdjson::SUCCESS);
        simdjson::ondemand::object object;
        error = doc.get_object().get(object);
        REQUIRE(error == simdjson::SUCCESS);
        error = city::simulator_metadata_from_json(object, &metadata);
        CHECK(error != simdjson::SUCCESS);
        REQUIRE(metadata.scenarios.size() == 1);
        CHECK(metadata.scenarios.data() == previous);
        CHECK(metadata.scenarios[0].name == "unchanged");
        CHECK(metadata.scenarios[0].timestamp_start == 10);
        CHECK(metadata.scenarios[0].timestamp_end == 20);
    }
}

TEST_CASE("Simulator metadata reply round-trips names and the playback time range")
{
    Arena* arena = arena_alloc();
    defer(arena_release(arena));
    String8 scenarios[] = {S("1. Scenario"), S("escaped\"name")};
    String8 reply = simulator_metadata_reply(arena, scenarios, ArrayCount(scenarios), -1.25, 12345.5);
    REQUIRE(reply.size > 0);
    simdjson::ondemand::parser parser;
    std::string_view input((char*)reply.str, reply.size);
    simdjson::padded_string json(input);
    simdjson::ondemand::document doc;
    auto error = parser.iterate(json).get(doc);
    REQUIRE(error == simdjson::SUCCESS);
    simdjson::ondemand::object object;
    error = doc.get_object().get(object);
    REQUIRE(error == simdjson::SUCCESS);
    U64 msg_id = 0;
    error = object["msg_id"].get_uint64().get(msg_id);
    REQUIRE(error == simdjson::SUCCESS);
    CHECK(msg_id == (U64)city::SimulationMessageKind::MetadataRequest);
    city::SimulationMetadata metadata = {};
    error = city::simulator_metadata_from_json(object, &metadata);
    REQUIRE(error == simdjson::SUCCESS);
    REQUIRE(metadata.scenarios.size() == ArrayCount(scenarios));
    for (U64 idx = 0; idx < metadata.scenarios.size(); ++idx)
    {
        CHECK(metadata.scenarios[idx].name == std::string_view((char*)scenarios[idx].str, scenarios[idx].size));
        CHECK(metadata.scenarios[idx].timestamp_start == -1.25);
        CHECK(metadata.scenarios[idx].timestamp_end == 12345.5);
    }
}

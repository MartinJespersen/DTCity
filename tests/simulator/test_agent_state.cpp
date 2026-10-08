#include "../test_inc.hpp"

TEST_CASE("Agent arrivals do not allocate slots and completed trips reuse pool capacity")
{
    city::AgentSim simulation(256, 2);
    city::AgentUpdate update = {};
    update.event_type = (S64)city::AgentEventType::Arrival;
    for (S64 id = 0; id < 15000; ++id)
    {
        update.id = id;
        city::AgentSlotUpdate slot = simulation.update_slot(update);
        CHECK_FALSE(slot.active);
    }
    CHECK(simulation.agents_active.item_in_use_count == 0);
    for (S64 id = 0; id < 15000; ++id)
    {
        update.id = id;
        update.event_type = 3;
        city::AgentSlotUpdate slot = simulation.update_slot(update);
        REQUIRE(slot.active);
        REQUIRE(slot.created);
        CHECK(simulation.agents_active.item_in_use_count == 1);
        update.event_type = (S64)city::AgentEventType::Arrival;
        slot = simulation.update_slot(update);
        CHECK_FALSE(slot.active);
        CHECK(simulation.agents_active.item_in_use_count == 0);
    }
}

TEST_CASE("Repeated agent updates keep one slot and reactivation invalidates old height handles")
{
    city::AgentSim simulation(8, 1);
    city::AgentUpdate update = {.id = 7, .event_type = 3};
    city::AgentSlotUpdate slot = simulation.update_slot(update);
    REQUIRE(slot.active);
    ArrayResourcePoolHandle old_handle = slot.agent->handle;
    slot.agent->height = 50;
    update.event_type = 1;
    slot = simulation.update_slot(update);
    REQUIRE(slot.active);
    CHECK_FALSE(slot.created);
    CHECK(slot.agent->height == 50);
    CHECK(simulation.agents_active.item_in_use_count == 1);

    update.event_type = (S64)city::AgentEventType::Arrival;
    slot = simulation.update_slot(update);
    CHECK_FALSE(slot.active);
    bool old_handle_valid = simulation.agents_active.is_handle_valid(old_handle);
    CHECK_FALSE(old_handle_valid);
    slot = simulation.update_slot(update);
    CHECK_FALSE(slot.active);
    CHECK(simulation.agents_active.item_in_use_count == 0);

    update.event_type = 3;
    slot = simulation.update_slot(update);
    REQUIRE(slot.active);
    CHECK(slot.created);
    CHECK(slot.agent->height == 0);
    CHECK(slot.agent->handle.idx == old_handle.idx);
    CHECK(slot.agent->handle.gen_id != old_handle.gen_id);
    CHECK(simulation.agents_active.item_in_use_count == 1);
}

TEST_CASE("Full snapshots retire absent agents before allocating replacements")
{
    city::AgentSim simulation(8, 1);
    city::AgentUpdate first = {.id = 1, .event_type = 3};
    city::AgentSlotUpdate slot = simulation.update_slot(first);
    REQUIRE(slot.active);
    ArrayResourcePoolHandle old_handle = slot.agent->handle;
    slot.agent->height = 50;

    // Polling skipped the first agent's arrival and now contains another active agent.
    city::AgentUpdate replacement = {.id = 2, .event_type = 1};
    Buffer<city::AgentUpdate> snapshot = {.data = &replacement, .size = 1};
    simulation.reconcile_snapshot(snapshot);
    CHECK(simulation.agents_active.item_in_use_count == 0);
    slot = simulation.update_slot(replacement);
    REQUIRE(slot.active);
    CHECK(slot.created);
    CHECK(simulation.agents_active.item_in_use_count == 1);
    bool old_valid = simulation.agents_active.is_handle_valid(old_handle);
    CHECK_FALSE(old_valid);

    slot.agent->height = 75;
    simulation.reconcile_snapshot(snapshot);
    slot = simulation.update_slot(replacement);
    REQUIRE(slot.active);
    CHECK_FALSE(slot.created);
    CHECK(slot.agent->height == 75);

    replacement.event_type = (S64)city::AgentEventType::Arrival;
    simulation.reconcile_snapshot(snapshot);
    CHECK(simulation.agents_active.item_in_use_count == 0);
    simulation.reconcile_snapshot({});
    CHECK(simulation.agents_active.item_in_use_count == 0);
}

TEST_CASE("Delayed full snapshots do not recreate completed trips in a full pool")
{
    ScratchScope scratch = ScratchScope(0, 0);
    city::AgentSim simulation(8, 2);
    city::AgentUpdate events[] = {
        {.id = 1, .time = 104, .event_type = 1},
        {.id = 1, .time = 106, .event_type = (S64)city::AgentEventType::Arrival},
        {.id = 2, .time = 107, .event_type = 3},
        {.id = 3, .time = 108, .event_type = 3},
        {.id = 2, .time = 110, .event_type = (S64)city::AgentEventType::Arrival},
    };
    ArrayResourcePoolHandle handles[2] = {};
    for (U64 index = 0; index < 2; ++index)
    {
        city::AgentSlotUpdate slot = simulation.update_slot(events[index + 2]);
        REQUIRE(slot.active);
        slot.agent->height = 50 + index;
        handles[index] = slot.agent->handle;
    }

    Buffer<city::AgentUpdate> pending = {.data = events, .size = ArrayCount(events)};
    Buffer<city::AgentUpdate> ready = city::simulator_events_before_playback(scratch.arena, pending, 110);
    ready = city::simulator_snapshot_latest_events(scratch.arena, ready);
    REQUIRE(ready.size == 3);
    CHECK(ready[0].event_type == (S64)city::AgentEventType::Arrival);
    REQUIRE(pending.size == 1);
    CHECK(pending[0].time == 110);
    simulation.reconcile_snapshot(ready);
    for (U64 index = 0; index < ready.size; ++index)
    {
        city::AgentSlotUpdate slot = simulation.update_slot(ready[index]);
        CHECK_FALSE(slot.created);
        if (slot.active)
        {
            CHECK(slot.agent->handle.idx == handles[index - 1].idx);
            CHECK(slot.agent->handle.gen_id == handles[index - 1].gen_id);
            CHECK(slot.agent->height == 49 + index);
        }
    }
    CHECK(simulation.agents_active.item_in_use_count == 2);

    // A retained future arrival is still applied on a later frame.
    ready = city::simulator_events_before_playback(scratch.arena, pending, 111);
    REQUIRE(ready.size == 1);
    city::AgentSlotUpdate slot = simulation.update_slot(ready[0]);
    CHECK_FALSE(slot.active);
    CHECK(simulation.agents_active.item_in_use_count == 1);
}

TEST_CASE("Snapshot compaction preserves final event order at equal timestamps")
{
    ScratchScope scratch = ScratchScope(0, 0);
    city::AgentUpdate events[] = {
        {.id = 7, .time = 100, .event_type = 3},
        {.id = 8, .time = 100, .event_type = 3},
        {.id = 7, .time = 100, .event_type = (S64)city::AgentEventType::Arrival},
        {.id = 7, .time = 100, .event_type = 1},
    };
    Buffer<city::AgentUpdate> ready = {.data = events, .size = ArrayCount(events)};
    ready = city::simulator_snapshot_latest_events(scratch.arena, ready);
    REQUIRE(ready.size == 2);
    CHECK(ready[0].id == 8);
    CHECK(ready[1].id == 7);
    CHECK(ready[1].event_type == 1);
    ready = city::simulator_snapshot_latest_events(scratch.arena, {});
    CHECK(ready.size == 0);
}

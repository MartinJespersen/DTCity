#include "../test_inc.hpp"

TEST_CASE("Simulations in a pool own independent agents counts and reset epochs")
{
    Allocator allocator = Allocator::create();
    ResourcePool<city::AgentSim> simulations(allocator.arena, 2);
    PoolHandle first_handle = simulations.emplace(8, 4);
    PoolHandle second_handle = simulations.emplace(8, 4);
    city::AgentSim* first = {};
    city::AgentSim* second = {};
    REQUIRE(simulations.item_from_handle(first_handle, &first));
    REQUIRE(simulations.item_from_handle(second_handle, &second));
    city::AgentUpdate update = {.id = 1, .event_type = 3, .way_id = 123};
    first->update_slot(update);
    update.way_id = 456;
    second->update_slot(update);
    CHECK(*first->agent_count_by_way.get(123) == 1);
    CHECK(first->agent_count_by_way.get(456) == nullptr);
    CHECK(*second->agent_count_by_way.get(456) == 1);
    city::AgentSim* original = first;
    first->reset();
    REQUIRE(simulations.item_from_handle(first_handle, &first));
    CHECK(first == original);
    CHECK(first->height_epoch == 1);
    CHECK(second->height_epoch == 0);
    CHECK(first->agents_active.item_in_use_count == 0);
    CHECK(second->agents_active.item_in_use_count == 1);
    CHECK(*second->agent_count_by_way.get(456) == 1);
    simulations.item_free(first_handle);
    PoolHandle replacement = simulations.emplace(8, 4);
    CHECK(replacement.idx == first_handle.idx);
    CHECK(replacement.gen_id != first_handle.gen_id);
    CHECK_FALSE(simulations.item_from_handle(first_handle, &first));
    REQUIRE(simulations.item_from_handle(replacement, &first));
    CHECK(first->agent_count_by_way.get(123) == nullptr);
    CHECK(*second->agent_count_by_way.get(456) == 1);
}

TEST_CASE("Way agent counts follow membership rather than repeated motion events")
{
    city::AgentSim simulation(8, 4);
    city::AgentUpdate first = {.id = 1, .event_type = 3, .node_from_id = 10, .node_to_id = 20, .way_id = 123};
    city::AgentUpdate second = {.id = 2, .event_type = 3, .node_from_id = 20, .node_to_id = 30, .way_id = 123};
    simulation.update_slot(first);
    simulation.update_slot(second);
    REQUIRE(simulation.agent_count_by_way.get(123));
    CHECK(*simulation.agent_count_by_way.get(123) == 2);

    // Updates and different segments on the same way cannot count an agent twice.
    first.node_from_id = 30;
    first.node_to_id = 20;
    simulation.update_slot(first);
    simulation.update_slot(first);
    CHECK(*simulation.agent_count_by_way.get(123) == 2);

    // Zero and signed way IDs are valid keys, independent of the GPU empty slot.
    first.way_id = 0;
    simulation.update_slot(first);
    CHECK(*simulation.agent_count_by_way.get(123) == 1);
    REQUIRE(simulation.agent_count_by_way.get(0));
    CHECK(*simulation.agent_count_by_way.get(0) == 1);
    second.way_id = -7;
    simulation.update_slot(second);
    CHECK(*simulation.agent_count_by_way.get(123) == 0);
    REQUIRE(simulation.agent_count_by_way.get(-7));
    CHECK(*simulation.agent_count_by_way.get(-7) == 1);

    // Retirement uses the stored membership, even if the arrival names another way.
    first.event_type = (S64)city::AgentEventType::Arrival;
    first.way_id = 999;
    simulation.update_slot(first);
    simulation.update_slot(first);
    CHECK(*simulation.agent_count_by_way.get(0) == 0);
    CHECK(simulation.agent_count_by_way.get(999) == nullptr);
    CHECK(*simulation.agent_count_by_way.get(-7) == 1);
    first.event_type = 3;
    first.way_id = 123;
    simulation.update_slot(first);
    CHECK(*simulation.agent_count_by_way.get(123) == 1);
}

TEST_CASE("Snapshots and resets clear way counts without rebuilding the road network")
{
    city::AgentSim simulation(8, 4);
    city::AgentUpdate events[] = {
        {.id = 1, .event_type = 3, .way_id = 123},
        {.id = 2, .event_type = 3, .way_id = 123},
        {.id = 3, .event_type = 3, .way_id = 456},
    };
    for (auto event : events) simulation.update_slot(event);
    city::AgentUpdate snapshot[] = {
        {.id = 2, .event_type = 1, .way_id = 456},
        {.id = 3, .event_type = (S64)city::AgentEventType::Arrival, .way_id = 456},
        {.id = 4, .event_type = 3, .way_id = 456},
    };
    Buffer<city::AgentUpdate> updates = {.data = snapshot, .size = ArrayCount(snapshot)};
    simulation.reconcile_snapshot(updates);
    for (auto event : updates) simulation.update_slot(event);
    CHECK(*simulation.agent_count_by_way.get(123) == 0);
    CHECK(*simulation.agent_count_by_way.get(456) == 2);
    simulation.reconcile_snapshot(updates);
    for (auto event : updates) simulation.update_slot(event);
    CHECK(*simulation.agent_count_by_way.get(456) == 2);
    simulation.reconcile_snapshot({});
    CHECK(*simulation.agent_count_by_way.get(456) == 0);

    simulation.update_slot(events[0]);
    simulation.reset();
    CHECK(simulation.agents_active.item_in_use_count == 0);
    CHECK(simulation.agent_count_by_way.get(123) == nullptr);
    CHECK(simulation.agent_count_by_way.get(456) == nullptr);
    simulation.update_slot(events[0]);
    simulation.update_slot(events[0]);
    CHECK(*simulation.agent_count_by_way.get(123) == 1);
}

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
    city::AgentUpdate update = {.id = 7, .event_type = 3, .node_from_id = 10, .node_to_id = 20,
                                .way_id = 1190023434};
    city::AgentSlotUpdate slot = simulation.update_slot(update);
    REQUIRE(slot.active);
    PoolHandle old_handle = slot.agent->handle;
    slot.agent->height = 50;
    update.event_type = 1;
    slot = simulation.update_slot(update);
    REQUIRE(slot.active);
    CHECK_FALSE(slot.created);
    CHECK(slot.agent->height == 50);
    CHECK(slot.agent->node_from_id == 10);
    CHECK(slot.agent->node_to_id == 20);
    CHECK(slot.agent->way_id == 1190023434);
    CHECK(simulation.agents_active.item_in_use_count == 1);

    // A zero way ID must replace the previous way's identity.
    update.way_id = 0;
    slot = simulation.update_slot(update);
    REQUIRE(slot.active);
    CHECK(slot.agent->way_id == 0);

    update.event_type = (S64)city::AgentEventType::Arrival;
    slot = simulation.update_slot(update);
    CHECK_FALSE(slot.active);
    bool old_handle_valid = simulation.agents_active.is_handle_valid(old_handle);
    CHECK_FALSE(old_handle_valid);
    slot = simulation.update_slot(update);
    CHECK_FALSE(slot.active);
    CHECK(simulation.agents_active.item_in_use_count == 0);

    update = {.id = 7, .event_type = 3};
    slot = simulation.update_slot(update);
    REQUIRE(slot.active);
    CHECK(slot.created);
    CHECK(slot.agent->height == 0);
    CHECK(slot.agent->node_from_id == 0);
    CHECK(slot.agent->node_to_id == 0);
    CHECK(slot.agent->way_id == 0);
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
    PoolHandle old_handle = slot.agent->handle;
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
    PoolHandle handles[2] = {};
    for (U64 index = 0; index < 2; ++index)
    {
        city::AgentSlotUpdate slot = simulation.update_slot(events[index + 2]);
        REQUIRE(slot.active);
        slot.agent->height = 50 + index;
        handles[index] = slot.agent->handle;
    }

    Buffer<city::AgentUpdate> pending = {.data = events, .size = ArrayCount(events)};
    Buffer<city::AgentUpdate> ready = city::simulator_events_ready(scratch.arena, pending, 109);
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
    ready = city::simulator_events_ready(scratch.arena, pending, 110);
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

TEST_CASE("Agent motion uses event time without frame accumulation and keeps its heading when stopped")
{
    city::AgentSim simulation(8, 1);
    city::AgentUpdate entry = {.id = 7, .time = 100, .event_type = 1,
                               .position = {4000000, 3000000, 2000000},
                               .direction = {0, 1, 0}, .speed = 8};
    city::AgentSlotUpdate slot = simulation.update_slot(entry);
    REQUIRE(slot.active);
    city::Agent* agent = slot.agent;
    simulation.update_positions(102.5);
    CHECK(agent->ecef_coord_cur.y == 3000020);
    CHECK(agent->ecef_coord_cur.x == 4000000);
    CHECK(agent->ecef_dir.y == 1);
    CHECK(agent->transform_dirty);
    agent->transform_dirty = false;
    simulation.update_positions(102.5);
    CHECK(agent->ecef_coord_cur.y == 3000020);
    CHECK_FALSE(agent->transform_dirty);
    simulation.update_positions(104);
    CHECK(agent->ecef_coord_cur.y == 3000032);
    CHECK(agent->transform_dirty);

    city::AgentUpdate exit = {.id = 7, .time = 105, .event_type = 2,
                              .position = {4000000, 3000040, 2000000}};
    simulation.update_slot(exit);
    simulation.update_positions(107);
    CHECK(agent->ecef_coord_cur.y == 3000040);
    CHECK(agent->ecef_velocity.y == 0);
    CHECK(agent->ecef_dir.y == 1);

    city::AgentUpdate turn = {.id = 7, .time = 108, .event_type = 1,
                              .position = exit.position, .direction = {0, 0, -1}, .speed = 2};
    simulation.update_slot(turn);
    simulation.update_positions(110);
    CHECK(agent->ecef_coord_cur.y == 3000040);
    CHECK(agent->ecef_coord_cur.z == 1999996);
    CHECK(agent->ecef_dir.z == -1);

    // A backward seek replaces the motion anchor with the earlier event.
    simulation.update_slot(entry);
    simulation.update_positions(101);
    CHECK(agent->ecef_coord_cur.y == 3000008);
    CHECK(agent->ecef_coord_cur.z == 2000000);
    CHECK(agent->ecef_dir.y == 1);
    agent->height = 50;
    city::AgentUpdate arrival = {.id = 7, .time = 105, .event_type = (S64)city::AgentEventType::Arrival};
    simulation.update_slot(arrival);
    slot = simulation.update_slot(exit);
    REQUIRE(slot.created);
    simulation.update_positions(110);
    CHECK(slot.agent->ecef_velocity.y == 0);
    CHECK(slot.agent->ecef_coord_cur.y == 3000040);
    CHECK(slot.agent->height == 0);
    CHECK(slot.agent->ecef_dir.x == 1);
}

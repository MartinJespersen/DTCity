#include "base/base_inc.hpp"
#include <glm/glm.hpp>
#include "render/render.hpp"
#include "city/simulator_messages.hpp"
#include "city/agent.hpp"
#include "city/simulator_shared_interface.hpp"

namespace city
{

AgentSim::AgentSim(U32 hashmap_bucket_capacity, U32 max_agent_count)
    : allocator(Allocator::create()), height_updates_stop(false),
      agent_map(&this->allocator, hashmap_bucket_capacity),
      agent_count_by_way(&this->allocator, hashmap_bucket_capacity),
      agents_active(this->allocator.arena, max_agent_count)
{
}

AgentSim::~AgentSim() noexcept
{
    if (this->height_sampler)
        this->height_sampler->destroy();
}

AgentSystem::AgentSystem(Arena* arena, U32 simulation_capacity) noexcept
    : simulations(arena, simulation_capacity)
{
}

PoolHandle
AgentSystem::simulation_create(U32 bucket_capacity, U32 agent_capacity) noexcept
{
    PoolHandle handle = this->simulations.emplace(bucket_capacity, agent_capacity);
    if (!this->simulations.is_handle_valid(this->selected_simulation))
        this->selected_simulation = handle;
    return handle;
}

bool
AgentSystem::simulation_select(PoolHandle handle) noexcept
{
    AgentSim* simulation = {};
    if (!this->simulations.item_from_handle(handle, &simulation))
        return false;
    this->selected_simulation = handle;
    for (Agent& agent : simulation->agents_active)
        agent.transform_dirty = true;
    return true;
}

void
AgentSystem::simulation_remove(PoolHandle handle) noexcept
{
    this->simulations.item_free(handle);
    if (handle == this->selected_simulation)
        this->selected_simulation = {};
}

AgentSlotUpdate
AgentSim::update_slot(const AgentUpdate& update)
{
    AgentMapItem* map_item = {};
    MapResult result = this->agent_map.get(update.id, &map_item);
    AgentSlotUpdate slot = {};
    if (result == MapResult::Success && map_item->agent_handle.idx)
    {
        slot.active = this->agents_active.item_from_handle(map_item->agent_handle, &slot.agent);
        Assert(slot.active);
    }

    // Arrival retires an existing agent; historical arrivals never allocate a slot.
    if (update.event_type == (S64)AgentEventType::Arrival)
    {
        if (slot.active)
        {
            this->way_count_change(slot.agent->way_id, -1);
            this->agents_active.item_free(map_item->agent_handle);
            map_item->agent_handle = {};
        }
        return {};
    }

    if (!slot.active)
    {
        PoolHandle handle = this->agents_active.item_new(&slot.agent);
        *slot.agent = {};
        slot.agent->handle = handle;
        slot.agent->id = update.id;
        if (result == MapResult::NotFound)
        {
            AgentMapItem item = {.agent_handle = handle};
            this->agent_map.insert(this->allocator.arena, update.id, item);
        }
        else
        {
            map_item->agent_handle = handle;
        }
        slot.active = true;
        slot.created = true;
    }
    Agent* agent = slot.agent;
    if (slot.created || agent->way_id != update.way_id)
    {
        if (!slot.created) this->way_count_change(agent->way_id, -1);
        this->way_count_change(update.way_id, 1);
    }
    agent->vehicle_type = update.vehicle_type;
    agent->node_from_id = update.node_from_id;
    agent->node_to_id = update.node_to_id;
    agent->way_id = update.way_id;
    agent->ecef_coord_at_event = update.position;
    agent->event_time = update.time;
    agent->ecef_velocity = update.direction * update.speed;
    // Keep the last heading when a zero-speed exit has no direction.
    if (glm::dot(update.direction, update.direction) > 0)
        agent->ecef_dir = update.direction;
    else if (slot.created)
        agent->ecef_dir = glm::dvec3(1, 0, 0);
    agent->transform_dirty = true;
    return slot;
}

void
AgentSim::update_positions(F64 playback_time)
{
    for (Agent& agent : this->agents_active)
    {
        F64 elapsed = playback_time - agent.event_time;
        glm::dvec3 position = agent.ecef_coord_at_event + agent.ecef_velocity * elapsed;
        if (position != agent.ecef_coord_cur)
        {
            agent.ecef_coord_cur = position;
            agent.transform_dirty = true;
        }
    }
}

void
AgentSim::reconcile_snapshot(Buffer<AgentUpdate> updates)
{
    // Full replies describe current state, so missed arrivals cannot leave ghost agents.
    ScratchScope scratch = ScratchScope(&this->allocator.arena, 1);
    Map<WsId, B32>* active_ids = Map<WsId, B32>::create(scratch.arena, this->agent_map.capacity);
    for (U64 index = 0; index < updates.size; ++index)
    {
        AgentUpdate* update = &updates.data[index];
        B32 active = update->event_type != (S64)AgentEventType::Arrival;
        B32* previous = {};
        MapResult result = active_ids->get(update->id, &previous);
        if (result == MapResult::Success)
        {
            *previous = active;
        }
        else
        {
            active_ids->insert(scratch.arena, update->id, active);
        }
    }
    for (Agent& agent : this->agents_active)
    {
        B32* active = {};
        MapResult result = active_ids->get(agent.id, &active);
        if (result == MapResult::NotFound || !*active)
        {
            AgentMapItem* map_item = {};
            MapResult mapped = this->agent_map.get(agent.id, &map_item);
            Assert(mapped == MapResult::Success);
            this->way_count_change(agent.way_id, -1);
            this->agents_active.item_free(agent.handle);
            map_item->agent_handle = {};
        }
    }
}

void
AgentSim::reset() noexcept
{
    ++this->height_epoch;
    this->agents_active.invalidate_all();
    this->agent_map.clear();
    this->agent_count_by_way.clear();
}

void
AgentSim::way_count_change(osm::WayId way_id, S32 delta) noexcept
{
    U32* count = {};
    if (this->agent_count_by_way.get(way_id, &count) == MapResult::NotFound)
    {
        Assert(delta > 0);
        count = this->agent_count_by_way.insert(this->allocator.arena, way_id, 0);
    }
    Assert(delta == 1 || (delta == -1 && *count > 0));
    // Keep zero-count entries so repeated trips reuse their storage.
    if (delta > 0) ++*count;
    else --*count;
}

} // namespace city

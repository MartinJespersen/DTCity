namespace city
{

AgentSim::AgentSim(U32 hashmap_bucket_capacity, U32 max_agent_count)
    : allocator(Allocator::create()), height_updates_stop(false), height_update_in_flight(false),
      agent_map(&this->allocator, hashmap_bucket_capacity),
      agents_active(this->allocator.arena, max_agent_count)
{
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
            this->agents_active.item_free(map_item->agent_handle);
            map_item->agent_handle = {};
        }
        return {};
    }

    if (!slot.active)
    {
        ArrayResourcePoolHandle handle = this->agents_active.item_new(&slot.agent);
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
    return slot;
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
            this->agents_active.item_free(agent.handle);
            map_item->agent_handle = {};
        }
    }
}

} // namespace city

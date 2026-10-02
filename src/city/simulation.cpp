namespace city
{
Simulation::Update
Simulation::update(Arena* arena, const ServerUpdate& input, bool playback_changed)
{
    bool scenario_changed = input.name != last_request.name;
    bool agents_clear = scenario_changed || playback_changed;
    bool period_changed = input.period != last_request.period;
    U64 now_us = os_now_microseconds();
    F64 refresh_seconds = Max(input.period * 0.5, 0.05);
    F64 request_elapsed = (F64)(now_us - request_sent_at_us) / 1'000'000.0;
    bool request_due = request_id == 0 || agents_clear || period_changed || request_elapsed >= refresh_seconds;
    if (agents_clear)
    {
        pending_events = {};
        arena_clear(event_allocator.arena);
    }
    if (client->connected && request_due)
    {
        if (request_id == 0 || agents_clear || period_changed)
        {
            ++request_id;
        }
        this->client->metadata_request();
        client->server_update_send(input, request_id);
        last_request = input;
        request_sent_at_us = now_us;
    }

    SimulationClient::StreamUpdate received = client->update(arena);
    if (has_flag(received.update_type, SimulationClient::UpdateType::Metadata))
    {
        metadata = std::move(received.metadata);
    }
    bool snapshot_received = has_flag(received.update_type, SimulationClient::UpdateType::Stream);
    if (snapshot_received)
    {
        // A reply contains a complete state plus lookahead, replacing the previous window.
        arena_clear(event_allocator.arena);
        pending_events = buffer_alloc<AgentUpdate>(event_allocator.arena, received.batch.size);
        for (U64 index = 0; index < received.batch.size; ++index)
        {
            pending_events.data[index] = received.batch.data[index];
            pending_events.data[index].original_id =
                push_str8_copy(event_allocator.arena, received.batch.data[index].original_id);
        }
    }
    Buffer<AgentUpdate> ready = simulator_events_before_playback(arena, pending_events, input.playback);
    if (snapshot_received)
    {
        // Apply current state without recreating trips that ended while the query ran.
        ready = simulator_snapshot_latest_events(arena, ready);
    }
    return {.updates = ready, .agents_clear = agents_clear, .snapshot_received = snapshot_received};
}

Simulation::Simulation() : event_allocator(Allocator::create())
{
    client = std::make_unique<SimulationClient>();
}

Simulation::~Simulation()
{
    stop();
}

SimulationError
Simulation::start()
{
    stop();
    SimulationError error = client->connect();
    return error;
}

void
Simulation::stop()
{
    client->disconnect();
    metadata.scenarios.clear();
    pending_events = {};
    arena_clear(event_allocator.arena);
    last_request = {};
    request_id = 0;
    request_sent_at_us = 0;
}
} // namespace city

namespace city
{
class Simulation
{
  public:
    SimulationMetadata metadata;

    struct Update
    {
        Buffer<AgentUpdate> updates;
        bool agents_clear;
        bool snapshot_received;
    };
    Simulation();
    ~Simulation();
    SimulationError
    start();
    Update
    update(Arena* arena, const ServerUpdate& input, bool playback_changed);
    void
    stop();

  private:
    std::unique_ptr<SimulationClient> client;
    Allocator event_allocator;
    Buffer<AgentUpdate> pending_events = {};
    ServerUpdate last_request = {};
    U64 request_id = 0;
    U64 request_sent_at_us = 0;
};
} // namespace city

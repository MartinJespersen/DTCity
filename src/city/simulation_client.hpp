#pragma once

namespace city
{

class SimulationClient
{
  private:
    // Created, used, and destroyed on the simulator's owner thread.

    std::vector<std::string> msg_send_queue;
    async::WebsocketConnection connection = {};
    U64 expected_request_id = 0;

  public:
    B32 connected = false;
    enum class UpdateType : U32
    {
        None = 0,
        Metadata = (1 << 0),
        Stream = (1 << 1)
    };
    ENABLE_BITMASK(UpdateType);

    struct StreamUpdate
    {
        UpdateType update_type;
        Buffer<AgentUpdate> batch;
        SimulationMetadata metadata;
    };

    SimulationClient() = default;
    ~SimulationClient();
    SimulationClient(const SimulationClient&) = delete;
    SimulationClient&
    operator=(const SimulationClient&) = delete;

    SimulationError
    connect();
    void
    disconnect();
    void
    server_update_send(const ServerUpdate& update, U64 request_id);
    SimulationClient::StreamUpdate
    update(Arena* arena);
    void
    metadata_request();

  private:
    SimulationError
    _update(Arena* arena, StreamUpdate* out_stream_update);
};
} // namespace city

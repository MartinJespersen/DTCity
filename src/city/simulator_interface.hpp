#pragma once

namespace city
{
class Simulator
{
  private:
    // Created, used, and destroyed on the simulator's owner thread.
    mi_heap_t* message_heap = {};
    U64 last_frame_update = {};
    U32 selected_scenario_idx = {};
    U32 status_update_freq_in_frames = {};

    Buffer<String8> msg_received = {};
    async::WebsocketConnection connection = {};
    // U32 cur_scenario_idx = 0;
    String8List msg_send_queue = {};
    // U64 cached_options_end = 0;
    B32 connected = false;
    // B32 session_restore_pending = false;

  public:
    Simulator() = default;
    ~Simulator();
    Simulator(const Simulator&) = delete;
    Simulator&
    operator=(const Simulator&) = delete;

    SimulationError
    simulator_connect(U32 status_update_freq_in_frames);
    void
    simulator_disconnect();
    void
    simulator_update(Arena* arena, CoordinateBatch* out_coords, String8List* in_out_options, U32 option_idx,
                     U64 cur_frame);
    void
    simulator_options_get();
    void
    simulator_snapshot_request();
    void
    simulator_scenario_set(U32 scenario_idx, Buffer<String8> options);

  private:
    SimulationError
    _simulator_interaction(Arena* arena, CoordinateBatch* out_coords, String8List* out_options,
                           B32* options_received, U32 expected_scenario_idx, U64* scenario_id);
};
} // namespace city

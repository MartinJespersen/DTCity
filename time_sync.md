# Time Sync Between Client and Server
Different scenarios:
- Real-time scenarios
  - Streamed source controls time.
- Playback
  - Visualizer/client controls time.
  - Streamed and simulated source gets should be stored for playback and fetched using a window

The playback client sends `ServerUpdate` with `name`, `playback`,
`period` (both times in seconds), and `request_id`. An empty name selects None.
Updates are sent immediately on scenario selection, seeking, or period changes,
and every half-period using the OS clock while playback continues or is paused.
The default period is 10 seconds.

The client progresses the WebSocket connection every frame, including while map
data and vehicle models load or the displayed area changes. Scenario controls
use the received metadata without waiting for those assets. When the agent
renderer becomes ready, the client requests a fresh snapshot for the current
playback time before resuming agent updates.

Metadata lists database filenames from `simulator/database/` without the final
`.sqlite`, `.sqlite3`, or `.db` extension. The query worker resolves the selected
name against those files, independently of the database loaded in the simulator UI.
Empty or unknown names return an empty stream. The existing metadata format still
uses the UI database's time range for all scenario names.

The simulator returns `Stream`, echoing `request_id`. Its chronological
`stream` contains the latest event strictly before playback for each active agent, plus
all events in `[playback, playback + period)`. The baseline allows backward seeks
and late arrivals to reconstruct state. SQLite queries run on a worker with its
own read-only connection. The simulator frame loop queues the latest request
and consumes completed replies without waiting for a query.

Each stream event contains `id`, `time`, `event_type`, `position_x`, `position_y`,
`position_z`, `direction_x`, `direction_y`, `direction_z`, `speed`,
`node_from_id`, `node_to_id`, and `way_id`. Position
is WGS84 ECEF in metres at the original event timestamp. Direction is an ECEF
unit vector, or zero when stationary. Speed is in metres per second. The server
sends the stored values without extrapolating them to playback. Node IDs are
signed 64-bit JSON integers copied from SQLite `node_from` and `node_to` columns.
Both IDs are required, including on arrivals without coordinates. Longitude/latitude endpoint fields are not sent.
`way_id` is the link's `origid` attribute (OSM way ID), stored as a signed 64-bit
SQLite integer and sent as a JSON integer. All three IDs are required; missing
or null IDs are rejected. Reimport older databases to populate these columns.
Only arrival events may have JSON `null` position components. Direction and speed
are always numeric. Events are ordered by `(time, agent_info_rowid)`, including
within each equal-timestamp group; clients must preserve that order.

The client retains future events in its simulation arena and applies only events
with `time <= playback`. Seeking or changing scenarios clears local agents and
increments `request_id`; replies for older requests are ignored. Periodic requests
retain that ID so network latency longer than the refresh interval cannot starve
playback. Playback stops at the final timestamp and applies the last event there.
Reset and scenario-ID messages are no longer part of this protocol.

Agent event type 4 is arrival. Seek baselines exclude agents whose last event is
arrival; future arrivals remain in the requested window. The client releases an
agent's pool slot on arrival and can reuse it for a later trip. Duplicate arrivals
and arrivals for unknown agents do not allocate slots.

On a complete reply, the client reconciles existing agents with the events ready
at playback before allocating new agents. Agents absent from that state are
retired, covering arrivals missed during delayed polling while preserving the
height and handle of agents that remain active.

## Client motion

`simulator_events_from_json` decodes `MotionEventView` into arena-owned
`AgentUpdate` values with double-precision position, direction, and speed.
It also preserves signed node IDs without conversion through floating point.
`AgentUpdate` and the active `Agent` expose `node_from_id`, `node_to_id`, and
`way_id`; zero is a valid ID. The way ID matches `osm::Edge::way_id`,
not the unique segment ID in `osm::Edge::id`; multiple links can share a way.
It reads the event type before position, so arrivals with null coordinates can
remove agents. Active events require numeric position and motion fields.

`AgentSim::update_slot` stores the latest event position, time, and ECEF velocity
(`direction * speed`). After applying ready events, `AgentSim::update_positions`
calculates each active agent's position on every frame:

```text
elapsed = playback_time - event_time
current_ecef = event_position + ecef_velocity * elapsed
```

The calculation uses the original event position, including frames with no new
reply. Pause and playback speed changes therefore use the same calculation.
Zero-speed exits stop movement. Arrivals remove agents. Equal-time events keep
server order, so the last event at a timestamp determines the state there.

`AgentSystem::update` derives longitude/latitude for terrain sampling from the
current ECEF position, applies terrain height, and updates transforms and bounds.
The supplied direction sets model orientation; a zero direction retains the
previous heading. Height results are checked against the agent's current position
to reject samples taken more than five metres behind it.

Seek resets, request-ID checks, future event buffering, and snapshot
reconciliation remain in place. Tests cover null-position arrivals, equal-time
entry/exit order, stopped agents, movement between replies, backward seeks,
and removal at the final timestamp.

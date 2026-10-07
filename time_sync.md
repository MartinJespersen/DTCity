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

The client retains future events in its simulation arena and applies only events
with `time < playback`. Seeking or changing scenarios clears local agents and
increments `request_id`; replies for older requests are ignored. Periodic requests
retain that ID so network latency longer than the refresh interval cannot starve
playback. Running playback advances just beyond the final timestamp so the last
event is applied too. Reset and scenario-ID messages are no longer part of this protocol.

Agent event type 4 is arrival. Seek baselines exclude agents whose last event is
arrival; future arrivals remain in the requested window. The client releases an
agent's pool slot on arrival and can reuse it for a later trip. Duplicate arrivals
and arrivals for unknown agents do not allocate slots.

On a complete reply, the client reconciles existing agents with the events ready
at playback before allocating new agents. Agents absent from that state are
retired, covering arrivals missed during delayed polling while preserving the
height and handle of agents that remain active.

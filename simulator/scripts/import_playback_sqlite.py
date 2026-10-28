#!/usr/bin/env python3

import argparse
import math
import sqlite3
import xml.etree.ElementTree as ET
from contextlib import closing
from dataclasses import dataclass
from pathlib import Path

SIMULATOR_ROOT = Path(__file__).resolve().parent.parent

DEFAULT_EVENTS_DIRECTORY = SIMULATOR_ROOT / "input" / "event_files"
DEFAULT_NETWORK_PATH = SIMULATOR_ROOT / "input" / "network_file" / "network.xml"
DEFAULT_OUTPUT_DIRECTORY = SIMULATOR_ROOT / "output"
# Persisted IDs: keep existing assignments stable when adding event types.
EVENT_TYPE_IDS = {
    "entered link": 1,
    "left link": 2,
    "vehicle enters traffic": 3,
    "vehicle leaves traffic": 4,
}


@dataclass(frozen=True, slots=True)
class NetworkLink:
    node_from: int
    node_to: int
    position_from: tuple | None
    position_to: tuple | None
    way_id: int


@dataclass(frozen=True)
class MovementEvent:
    index: int
    time: float
    event_type: int
    agent_id: str | None
    link_id: str | None
    position: tuple | None
    attributes: dict


def parse_args():
    parser = argparse.ArgumentParser(
        description="Build one SQLite motion database per MATSim events XML file."
    )
    parser.add_argument("--events", nargs="+", default=[DEFAULT_EVENTS_DIRECTORY],
                        help="Events XML files or folders. A file imports only that file; a folder imports its XML files. "
                             "Default: simulator/input/event_files")
    parser.add_argument("--network", default=DEFAULT_NETWORK_PATH)
    parser.add_argument("--network-crs", help="Override the network CRS (otherwise use XML metadata, then EPSG:21781)")
    parser.add_argument("--output", default=DEFAULT_OUTPUT_DIRECTORY,
                        help="Output folder (default: simulator/output); each XML becomes <name>.sqlite")
    parser.add_argument("--limit-events", type=int, default=0, help="Maximum events scanned per file; 0 means all")
    parser.add_argument("--batch-size", type=int, default=50000)
    args = parser.parse_args()
    if args.limit_events < 0:
        parser.error("--limit-events must be non-negative")
    if args.batch_size <= 0:
        parser.error("--batch-size must be positive")
    return args


def resolve_input_path(path_value):
    path = Path(path_value)
    if path.is_absolute():
        return path
    cwd_candidate = Path.cwd() / path
    if cwd_candidate.exists():
        return cwd_candidate.resolve()
    return (SIMULATOR_ROOT / path).resolve()


def discover_event_files(path_values, excluded_paths):
    """Collect events XML files without treating the shared network as events."""
    events_paths = set()
    for path_value in path_values:
        path = resolve_input_path(path_value)
        if path.is_dir():
            candidates = sorted(path.iterdir())
        elif path.is_file():
            candidates = [path]
        else:
            raise FileNotFoundError(f"MATSim events path not found: {path}")
        for candidate in candidates:
            if candidate.is_file() and candidate.suffix.lower() == ".xml":
                events_path = candidate.resolve()
                if events_path not in excluded_paths:
                    events_paths.add(events_path)
    if not events_paths:
        raise FileNotFoundError("No MATSim events XML files found in the events paths")
    return sorted(events_paths)


def unlink_existing_db(output_path):
    for path in (
        output_path,
        output_path.with_name(f"{output_path.name}-wal"),
        output_path.with_name(f"{output_path.name}-shm"),
    ):
        if path.exists():
            path.unlink()


def wgs84_to_ecef(longitude, latitude):
    """Convert WGS84 degrees at zero ellipsoid height to ECEF metres."""
    longitude = math.radians(longitude)
    latitude = math.radians(latitude)
    semi_major_axis = 6378137.0
    flattening = 1 / 298.257223563
    eccentricity_squared = flattening * (2 - flattening)
    sin_latitude = math.sin(latitude)
    cos_latitude = math.cos(latitude)
    radius = semi_major_axis / math.sqrt(1 - eccentricity_squared * sin_latitude**2)
    return (radius * cos_latitude * math.cos(longitude),
            radius * cos_latitude * math.sin(longitude),
            radius * (1 - eccentricity_squared) * sin_latitude)


def parse_network_id(value, description):
    try:
        identifier = int(value)
    except ValueError as exc:
        raise ValueError(f"Network {description} must be an integer: {value!r}") from exc
    if not -(2**63) <= identifier < 2**63:
        raise ValueError(f"Network {description} exceeds the signed 64-bit range: {value!r}")
    return identifier


def load_network_links(network_path, source_crs=None):
    """Resolve node IDs, OSM way IDs (origid), and ECEF endpoints."""
    link_nodes = {}
    node_coordinates = {}
    projected_coordinates = {}
    network_crs = None
    link_way_id = None
    # Remove completed elements from their parents to bound XML parsing memory.
    parents = []
    with open(network_path, "rb") as source:
        for event, element in ET.iterparse(source, events=("start", "end")):
            if event == "start":
                parents.append(element)
                if element.tag == "link":
                    link_way_id = None
                continue
            if (element.tag == "attribute"
                    and element.get("name") == "coordinateReferenceSystem"
                    and len(parents) == 3 and parents[0].tag == "network"):
                network_crs = (element.text or "").strip() or None
            elif (element.tag == "attribute" and element.get("name") == "origid"
                  and len(parents) >= 3 and parents[-2].tag == "attributes" and parents[-3].tag == "link"):
                # Capture the value before the bounded-memory parser removes the child.
                link_way_id = parse_network_id((element.text or "").strip(), "link origid")
            elif element.tag == "node":
                node_id = element.get("id")
                if node_id:
                    if element.get("lon") is not None and element.get("lat") is not None:
                        node_coordinates[node_id] = (float(element.get("lon")), float(element.get("lat")))
                    elif element.get("x") is not None and element.get("y") is not None:
                        projected_coordinates[node_id] = (float(element.get("x")), float(element.get("y")))
            elif element.tag == "link":
                link_id = element.get("id")
                node_from = element.get("from")
                node_to = element.get("to")
                if not link_id or not node_from or not node_to:
                    raise ValueError(f"Network link {link_id!r} requires id, from, and to attributes")
                if link_way_id is None:
                    raise ValueError(f"Network link {link_id!r} requires an origid attribute")
                link_nodes[link_id] = (node_from, node_to, link_way_id)
            parents.pop()
            if parents:
                parents[-1].remove(element)
            element.clear()
    if projected_coordinates:
        try:
            from pyproj import Transformer
        except ImportError as exc:
            raise RuntimeError("Install pyproj to convert network coordinates: python -m pip install pyproj") from exc
        transformer = Transformer.from_crs(
            source_crs or network_crs or "EPSG:21781", "EPSG:4326", always_xy=True
        )
        for node_id, (x, y) in projected_coordinates.items():
            node_coordinates[node_id] = transformer.transform(x, y, errcheck=True)
    node_positions = {node_id: wgs84_to_ecef(*coordinates)
                      for node_id, coordinates in node_coordinates.items()}
    return {link_id: NetworkLink(parse_network_id(node_from, "node ID"), parse_network_id(node_to, "node ID"),
                                node_positions.get(node_from), node_positions.get(node_to), way_id)
            for link_id, (node_from, node_to, way_id) in link_nodes.items()}


def calculate_motion(position_from, position_to, time_from, time_to):
    """Return ECEF unit direction and speed that reach the exit at its event time."""
    duration = time_to - time_from
    if duration < 0:
        raise ValueError("Link exit time precedes entry time")
    displacement = tuple(end - start for start, end in zip(position_from, position_to))
    distance = math.sqrt(sum(component**2 for component in displacement))
    # Equal timestamps are an instantaneous jump: the later exit row wins.
    if duration == 0 or distance == 0:
        return (0.0, 0.0, 0.0), 0.0
    return tuple(component / distance for component in displacement), distance / duration


def create_schema(connection):
    connection.executescript(
        """
        CREATE TABLE agent_events (
          agent_info_rowid INTEGER PRIMARY KEY,
          time REAL NOT NULL,
          event_type INTEGER NOT NULL,
          agent_id TEXT NOT NULL,
          position_x REAL,
          position_y REAL,
          position_z REAL,
          direction_x REAL NOT NULL,
          direction_y REAL NOT NULL,
          direction_z REAL NOT NULL,
          speed REAL NOT NULL,
          node_from INTEGER NOT NULL,
          node_to INTEGER NOT NULL,
          way_id INTEGER NOT NULL
        );
        CREATE INDEX agent_events_time ON agent_events(time);
        CREATE INDEX agent_events_agent_time ON agent_events(agent_id, time);
        """
    )


def flush_events(connection, event_rows):
    connection.executemany(
        """
        INSERT INTO agent_events (
          agent_info_rowid, time, event_type, agent_id, position_x, position_y, position_z,
          direction_x, direction_y, direction_z, speed, node_from, node_to, way_id
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        """,
        event_rows,
    )
    connection.commit()
    event_rows.clear()


def import_events(connection, events_path, network_links, limit_events, batch_size, on_unmatched_entry=None):
    """Write matched entry motion and stationary exits in source event order."""
    event_rows = []
    pending_entries = {}
    event_count = 0
    imported_count = 0
    skipped_count = 0
    parents = []
    with open(events_path, "rb") as source:
        for event, element in ET.iterparse(source, events=("start", "end")):
            if event == "start":
                parents.append(element)
                continue
            if element.tag == "event":
                event_count += 1
                event_type = EVENT_TYPE_IDS.get(element.get("type", ""))
                if event_type is not None:
                    agent_id = element.get("vehicle")
                    link_id = element.get("link")
                    time = float(element.attrib["time"])
                    if not math.isfinite(time):
                        raise ValueError(f"Non-finite time in event {event_count}")
                    if event_type == EVENT_TYPE_IDS["entered link"]:
                        relative_position = 0.0
                    elif event_type == EVENT_TYPE_IDS["left link"]:
                        relative_position = 1.0
                    else:
                        relative_position = element.get("relativePosition")
                        if relative_position is not None:
                            relative_position = float(relative_position)
                    if relative_position is not None and not 0 <= relative_position <= 1:
                        raise ValueError(f"Invalid relativePosition in event {event_count}")
                    position = None
                    link = network_links.get(link_id)
                    if link is None:
                        raise ValueError(f"Event {event_count} references unknown network link {link_id!r}")
                    node_ids = (link.node_from, link.node_to)
                    way_id = link.way_id
                    if (link.position_from is not None and link.position_to is not None
                            and relative_position is not None):
                        position = tuple(start + relative_position * (end - start)
                                         for start, end in zip(link.position_from, link.position_to))
                    current = MovementEvent(event_count, time, event_type, agent_id, link_id,
                                            position, dict(element.attrib))

                    # Match by vehicle and link before considering geometry.
                    is_entry = event_type in (EVENT_TYPE_IDS["entered link"], EVENT_TYPE_IDS["vehicle enters traffic"])
                    entry = pending_entries.pop(agent_id, None)
                    if entry is not None:
                        if not is_entry and link_id and entry.link_id == link_id:
                            if entry.position is not None and position is not None:
                                direction, speed = calculate_motion(entry.position, position, entry.time, time)
                                event_rows.append((entry.index, entry.time, entry.event_type, entry.agent_id,
                                                   *entry.position, *direction, speed, *node_ids, way_id))
                                imported_count += 1
                            else:
                                skipped_count += 1
                        else:
                            skipped_count += 1
                            if on_unmatched_entry is not None:
                                reason = "next_entry_before_exit" if is_entry else "exit_link_mismatch"
                                on_unmatched_entry(entry, reason, current)

                    if is_entry and agent_id:
                        pending_entries[agent_id] = current
                    elif is_entry:
                        skipped_count += 1
                        if on_unmatched_entry is not None:
                            on_unmatched_entry(current, "missing_vehicle", None)
                    elif agent_id and (position is not None or event_type == EVENT_TYPE_IDS["vehicle leaves traffic"]):
                        # Exits stop extrapolation. Arrivals still remove an agent when
                        # their position is unavailable; only those rows may lack a position.
                        exit_position = position if position is not None else (None, None, None)
                        event_rows.append((current.index, time, event_type, agent_id,
                                           *exit_position, 0.0, 0.0, 0.0, 0.0, *node_ids, way_id))
                        imported_count += 1
                    else:
                        skipped_count += 1
                    if len(event_rows) >= batch_size:
                        flush_events(connection, event_rows)
                        print(f"Scanned {event_count:,} events; wrote {imported_count:,} agent events...")
            parents.pop()
            if parents:
                parents[-1].remove(element)
            element.clear()
            if limit_events and event_count >= limit_events:
                break
    skipped_count += len(pending_entries)
    if on_unmatched_entry is not None:
        reason = "event_limit" if limit_events and event_count >= limit_events else "end_of_file"
        for entry in pending_entries.values():
            on_unmatched_entry(entry, reason, None)
    flush_events(connection, event_rows)
    return event_count, imported_count, skipped_count


def main():
    args = parse_args()
    network_path = resolve_input_path(args.network)
    if not network_path.is_file():
        raise FileNotFoundError(f"MATSim network XML not found: {network_path}")
    events_paths = discover_event_files(args.events, {network_path})
    output_directory = Path(args.output).resolve()

    # Check all output names before replacing any databases. Names must also be unique on Windows.
    output_paths = []
    output_names = set()
    input_paths = {*events_paths, network_path}
    for events_path in events_paths:
        output_name = events_path.with_suffix(".sqlite").name
        if output_name.casefold() in output_names:
            raise ValueError(f"Events filenames produce the same output database: {output_name}")
        output_names.add(output_name.casefold())
        output_path = output_directory / output_name
        if output_path in input_paths:
            raise ValueError("Output database must not overwrite an input file")
        output_paths.append(output_path)

    # Load the shared network once, then import each events file into its own database.
    network_links = load_network_links(network_path, args.network_crs)
    output_directory.mkdir(parents=True, exist_ok=True)
    for events_path, output_path in zip(events_paths, output_paths):
        print(f"Importing {events_path}...")
        try:
            unlink_existing_db(output_path)
        except OSError as exc:
            raise RuntimeError(
                f"Could not replace existing database at {output_path}. "
                "Make sure the controller or any other program is not using it."
            ) from exc
        with closing(sqlite3.connect(output_path)) as connection:
            create_schema(connection)
            event_count, imported_count, skipped_count = import_events(
                connection, events_path, network_links, args.limit_events, args.batch_size,
                on_unmatched_entry=create_unmatched_entry_debugger(connection, events_path),
            )
        print(f"Wrote {imported_count:,} agent events to {output_path}. Scanned {event_count:,} events.")
        print(f"Resolved {len(network_links):,} network links. "
              f"Skipped {skipped_count:,} movement events without a vehicle, position, or matching exit.")


# BEGIN TEMPORARY UNMATCHED-ENTRY DEBUGGING
# Remove this function and the on_unmatched_entry argument in main to disable
# diagnostics. The importer and the agent_events schema do not depend on this table.
def create_unmatched_entry_debugger(connection, events_path):
    import json

    connection.execute(
        """
        CREATE TABLE debug_unmatched_entries (
          event_index INTEGER PRIMARY KEY,
          source_file TEXT NOT NULL,
          time REAL NOT NULL,
          event_type TEXT NOT NULL,
          agent_id TEXT,
          link_id TEXT,
          reason TEXT NOT NULL,
          entry_attributes_json TEXT NOT NULL,
          next_event_index INTEGER,
          next_event_attributes_json TEXT
        )
        """
    )

    def record(entry, reason, next_event):
        connection.execute(
            "INSERT INTO debug_unmatched_entries VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
            (entry.index, str(events_path), entry.time, entry.attributes["type"],
             entry.agent_id, entry.link_id, reason, json.dumps(entry.attributes, sort_keys=True),
             next_event.index if next_event is not None else None,
             json.dumps(next_event.attributes, sort_keys=True) if next_event is not None else None),
        )

    return record
# END TEMPORARY UNMATCHED-ENTRY DEBUGGING


if __name__ == "__main__":
    main()

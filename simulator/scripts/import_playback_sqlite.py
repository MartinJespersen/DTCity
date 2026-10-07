#!/usr/bin/env python3

import argparse
import sqlite3
import xml.etree.ElementTree as ET
from contextlib import closing
from pathlib import Path

SIMULATOR_ROOT = Path(__file__).resolve().parent.parent

DEFAULT_EVENTS_DIRECTORY = SIMULATOR_ROOT / "input" / "event_files"
DEFAULT_NETWORK_PATH = SIMULATOR_ROOT / "input" / "network_file" / "network.xml"
DEFAULT_MAP_PATH = SIMULATOR_ROOT / "input" / "map_file"
DEFAULT_OUTPUT_DIRECTORY = SIMULATOR_ROOT / "output"
# Persisted IDs: keep existing assignments stable when adding event types.
EVENT_TYPE_IDS = {
    "entered link": 1,
    "left link": 2,
    "vehicle enters traffic": 3,
    "vehicle leaves traffic": 4,
}
EVENT_TYPE_NAMES = {event_id: name for name, event_id in EVENT_TYPE_IDS.items()}


def parse_args():
    parser = argparse.ArgumentParser(
        description="Build one SQLite database per MATSim events XML file, using shared network node IDs."
    )
    parser.add_argument("--events", nargs="+", default=[DEFAULT_EVENTS_DIRECTORY],
                        help="Events XML files or folders (default: simulator/input/event_files)")
    parser.add_argument("--network", default=DEFAULT_NETWORK_PATH)
    parser.add_argument("--map", default=DEFAULT_MAP_PATH,
                        help="Required map file or folder containing one map file (default: simulator/input/map_file)")
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


def resolve_output_path(path_value):
    return Path(path_value).resolve()


def discover_event_files(path_values, excluded_paths):
    """Collect events XML files without treating the shared network or map as events."""
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


def parse_node_id(value):
    """Convert a network node ID to SQLite's signed 64-bit integer range."""
    try:
        node_id = int(value)
    except ValueError as exc:
        raise ValueError(f"Network node ID must be an integer: {value!r}") from exc
    if not -(2**63) <= node_id < 2**63:
        raise ValueError(f"Network node ID exceeds SQLite's signed 64-bit range: {value!r}")
    return node_id


def load_link_nodes(network_path, source_crs=None):
    """Map links to endpoint IDs and WGS84 longitude/latitude coordinates."""
    link_nodes = {}
    node_coordinates = {}
    projected_coordinates = {}
    network_crs = None
    # Remove completed elements from their parents to bound XML parsing memory.
    parents = []
    for event, element in ET.iterparse(network_path, events=("start", "end")):
        if event == "start":
            parents.append(element)
            continue
        if (element.tag == "attribute"
                and element.get("name") == "coordinateReferenceSystem"
                and len(parents) == 3 and parents[0].tag == "network"):
            network_crs = (element.text or "").strip() or None
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
            if link_id and node_from and node_to:
                link_nodes[link_id] = (node_from, node_to)
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
    return {
        link_id: (parse_node_id(node_from), parse_node_id(node_to),
                  *node_coordinates.get(node_from, (None, None)),
                  *node_coordinates.get(node_to, (None, None)))
        for link_id, (node_from, node_to) in link_nodes.items()
    }


def create_schema(connection):
    connection.executescript(
        """
        CREATE TABLE agent_events (
          agent_info_rowid INTEGER PRIMARY KEY,
          time REAL NOT NULL,
          event_type INTEGER NOT NULL,
          agent_id TEXT NOT NULL,
          node_from INTEGER NOT NULL,
          node_to INTEGER NOT NULL,
          node_from_lon REAL,
          node_from_lat REAL,
          node_to_lon REAL,
          node_to_lat REAL
        );
        CREATE INDEX agent_events_time ON agent_events(time);
        CREATE INDEX agent_events_agent_time ON agent_events(agent_id, time);
        """
    )


def flush_events(connection, event_rows):
    if not event_rows:
        return
    connection.executemany(
        """
        INSERT INTO agent_events (
          agent_info_rowid, time, event_type, agent_id, node_from, node_to,
          node_from_lon, node_from_lat, node_to_lon, node_to_lat
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        """,
        event_rows,
    )
    connection.commit()
    event_rows.clear()


def import_events(connection, events_path, link_nodes, limit_events, batch_size):
    """Store movement events independently, preserving vehicle IDs and event times."""
    event_rows = []
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
                    nodes = link_nodes.get(element.get("link"))
                    if agent_id and nodes is not None:
                        imported_count += 1
                        event_rows.append((
                            imported_count,
                            float(element.attrib["time"]),
                            event_type,
                            agent_id,
                            *nodes,
                        ))
                        if len(event_rows) >= batch_size:
                            flush_events(connection, event_rows)
                            print(f"Scanned {event_count:,} events; wrote {imported_count:,} agent events...")
                    else:
                        skipped_count += 1
            parents.pop()
            if parents:
                parents[-1].remove(element)
            element.clear()
            if limit_events and event_count >= limit_events:
                break
    flush_events(connection, event_rows)
    return event_count, imported_count, skipped_count


def main():
    args = parse_args()
    network_path = resolve_input_path(args.network)
    if not network_path.is_file():
        raise FileNotFoundError(f"MATSim network XML not found: {network_path}")
    map_path = resolve_input_path(args.map)
    if map_path.is_dir():
        map_files = sorted(path for path in map_path.iterdir() if path.is_file())
        if len(map_files) != 1:
            raise ValueError(f"Map folder must contain exactly one map file: {map_path}. Use --map to select a file.")
        map_path = map_files[0].resolve()
    if not map_path.is_file():
        raise FileNotFoundError(f"Map file not found: {map_path}")
    events_paths = discover_event_files(args.events, {network_path, map_path})
    output_directory = resolve_output_path(args.output)

    # Check all output names before replacing any databases. Names must also be unique on Windows.
    output_paths = []
    output_names = set()
    input_paths = {*events_paths, network_path, map_path}
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
    link_nodes = load_link_nodes(network_path, args.network_crs)
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
                connection, events_path, link_nodes, args.limit_events, args.batch_size
            )
        print(f"Wrote {imported_count:,} agent events to {output_path}. Scanned {event_count:,} events.")
        print(f"Resolved {len(link_nodes):,} network links. "
              f"Skipped {skipped_count:,} movement events without a vehicle or known link.")


if __name__ == "__main__":
    main()

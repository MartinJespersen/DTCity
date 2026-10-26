import importlib.util
import io
import json
import math
import sqlite3
import sys
import tempfile
import unittest
from contextlib import closing, redirect_stdout
from pathlib import Path
from unittest.mock import patch


SCRIPT_PATH = Path(__file__).resolve().parents[2] / "simulator" / "scripts" / "import_playback_sqlite.py"
SPEC = importlib.util.spec_from_file_location("import_playback_sqlite", SCRIPT_PATH)
importer = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = importer
SPEC.loader.exec_module(importer)


class ImportPlaybackSqliteTests(unittest.TestCase):
    def setUp(self):
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary_directory.cleanup)
        self.root = Path(self.temporary_directory.name)
        self.events_directory = self.root / "input" / "event_files"
        self.events_directory.mkdir(parents=True)
        self.network_path = self.root / "input" / "network_file" / "network.xml"
        self.network_path.parent.mkdir()
        self.network_path.write_text(
            '<network><nodes><node id="1" lon="16" lat="59"/>'
            '<node id="2" lon="17" lat="60"/></nodes>'
            '<links><link id="road" from="1" to="2"/>'
            '<link id="road-back" from="2" to="1"/></links></network>',
            encoding="utf-8",
        )
        self.output_directory = self.root / "output"

    def write_events(self, name, vehicle, time):
        path = self.events_directory / name
        path.write_text(
            f'<events><event time="{time}" type="entered link" vehicle="{vehicle}" link="road"/>'
            f'<event time="{time + 1}" type="vehicle leaves traffic" vehicle="{vehicle}" '
            'link="road" relativePosition="1.0"/></events>',
            encoding="utf-8",
        )
        return path

    def run_import(self, extra_args=()):
        arguments = [str(SCRIPT_PATH), "--events", str(self.events_directory),
                     "--network", str(self.network_path),
                     "--output", str(self.output_directory),
                     *extra_args]
        with patch.object(sys, "argv", arguments), redirect_stdout(io.StringIO()):
            importer.main()

    def test_multiple_files_have_separate_rows_and_matching_names(self):
        self.write_events("city.day 1.xml", "bus-1", 10)
        self.write_events("town.XML", "car-2", 20)
        (self.events_directory / "notes.txt").write_text("ignored", encoding="utf-8")
        with patch.object(importer, "load_link_positions", wraps=importer.load_link_positions) as load_network:
            self.run_import(("--batch-size", "1"))
        load_network.assert_called_once()
        self.assertEqual({path.name for path in self.output_directory.iterdir()},
                         {"city.day 1.sqlite", "town.sqlite"})
        for name, vehicle, time in (("city.day 1.sqlite", "bus-1", 10), ("town.sqlite", "car-2", 20)):
            with self.subTest(name=name), closing(sqlite3.connect(self.output_directory / name)) as connection:
                columns = [column[1] for column in connection.execute("PRAGMA table_info(agent_events)")]
                self.assertEqual(columns, ["agent_info_rowid", "time", "event_type", "agent_id",
                                          "position_x", "position_y", "position_z",
                                          "direction_x", "direction_y", "direction_z", "speed"])
                rows = connection.execute("SELECT * FROM agent_events ORDER BY time").fetchall()
                self.assertEqual([row[:4] for row in rows], [(1, time, 1, vehicle), (2, time + 1, 4, vehicle)])
                start = importer.wgs84_to_ecef(16, 59)
                end = importer.wgs84_to_ecef(17, 60)
                self.assertEqual(rows[0][4:7], start)
                self.assertEqual(rows[1][4:7], end)
                self.assertEqual(rows[1][7:], (0, 0, 0, 0))
                for axis in range(3):
                    self.assertAlmostEqual(rows[0][4 + axis] + rows[0][7 + axis] * rows[0][10], end[axis])
                self.assertEqual(connection.execute("SELECT count(*) FROM debug_unmatched_entries").fetchone(), (0,))

    def test_ecef_coordinates_and_motion_calculation(self):
        for lon, lat, expected in ((0, 0, (6378137, 0, 0)),
                                   (90, 0, (0, 6378137, 0)),
                                   (0, 90, (0, 0, 6356752.314245179))):
            with self.subTest(lon=lon, lat=lat):
                for actual, coordinate in zip(importer.wgs84_to_ecef(lon, lat), expected):
                    self.assertAlmostEqual(actual, coordinate, places=6)
        direction, speed = importer.calculate_motion((1, 2, 3), (4, 6, 3), 10, 12)
        self.assertEqual(direction, (0.6, 0.8, 0))
        self.assertEqual(speed, 2.5)
        self.assertEqual(importer.calculate_motion((1, 2, 3), (1, 2, 3), 10, 12), ((0, 0, 0), 0))
        self.assertEqual(importer.calculate_motion((1, 2, 3), (4, 6, 3), 10, 10), ((0, 0, 0), 0))
        with self.assertRaisesRegex(ValueError, "exit time precedes entry"):
            importer.calculate_motion((1, 2, 3), (4, 6, 3), 10, 9)

    def test_link_intervals_match_vehicle_and_visit_across_batches(self):
        (self.events_directory / "city.xml").write_text(
            '''<events>
            <event time="0" type="actend" person="one"/>
            <event time="0.25" type="entered link" vehicle="one" link="road"/>
            <event time="0.5" type="entered link" vehicle="one_bicycle" link="road"/>
            <event time="1.75" type="left link" vehicle="one_bicycle" link="road"/>
            <event time="2.125" type="left link" vehicle="one" link="road"/>
            <event time="3" type="entered link" vehicle="one" link="road"/>
            <event time="3" type="left link" vehicle="one" link="road"/>
            </events>''', encoding="utf-8",
        )
        for batch_size in (1, 3, 50000):
            with self.subTest(batch_size=batch_size):
                self.run_import(("--batch-size", str(batch_size)))
                with closing(sqlite3.connect(self.output_directory / "city.sqlite")) as connection:
                    rows = connection.execute("SELECT * FROM agent_events ORDER BY agent_info_rowid").fetchall()
                    self.assertEqual([row[:4] for row in rows], [
                        (2, 0.25, 1, "one"), (3, 0.5, 1, "one_bicycle"),
                        (4, 1.75, 2, "one_bicycle"), (5, 2.125, 2, "one"),
                        (6, 3, 1, "one"), (7, 3, 2, "one"),
                    ])
                    start = importer.wgs84_to_ecef(16, 59)
                    end = importer.wgs84_to_ecef(17, 60)
                    distance = math.dist(start, end)
                    self.assertAlmostEqual(rows[0][10], distance / 1.875)
                    self.assertAlmostEqual(rows[1][10], distance / 1.25)
                    self.assertAlmostEqual(math.sqrt(sum(value**2 for value in rows[0][7:10])), 1)
                    # A reader needs only the latest row, including for stops and equal timestamps.
                    for playback, fraction in ((1.1875, 0.5), (2.5, 1), (3, 1), (100, 1)):
                        row = connection.execute(
                            "SELECT * FROM agent_events WHERE agent_id = 'one' AND time <= ? "
                            "ORDER BY time DESC, agent_info_rowid DESC LIMIT 1", (playback,),
                        ).fetchone()
                        for axis in range(3):
                            actual = row[4 + axis] + row[7 + axis] * row[10] * (playback - row[1])
                            self.assertAlmostEqual(actual, start[axis] + fraction * (end[axis] - start[axis]))

    def test_missing_exits_and_different_links_do_not_create_false_intervals(self):
        (self.events_directory / "city.xml").write_text(
            '''<events>
            <event time="1" type="entered link" vehicle="one" link="road" person="p&amp;x"/>
            <event time="2" type="left link" vehicle="one" link="unknown"/>
            <event time="3" type="left link" vehicle="one" link="road"/>
            <event time="4" type="entered link" vehicle="one" link="road"/>
            <event time="5" type="left link" vehicle="one" link="road-back"/>
            <event time="6" type="left link" vehicle="one" link="road"/>
            <event time="7" type="entered link" vehicle="one" link="road"/>
            <event time="8" type="vehicle enters traffic" vehicle="one" link="road" relativePosition="1"/>
            <event time="9" type="left link" vehicle="one" link="road"/>
            <event time="10" type="entered link" vehicle="one" link="road"/>
            </events>''', encoding="utf-8",
        )
        self.run_import()
        with closing(sqlite3.connect(self.output_directory / "city.sqlite")) as connection:
            rows = connection.execute("SELECT time, speed FROM agent_events ORDER BY time").fetchall()
            debug_rows = connection.execute(
                "SELECT event_index, reason, next_event_index FROM debug_unmatched_entries ORDER BY event_index"
            ).fetchall()
            source, entry_json, next_json = connection.execute(
                "SELECT source_file, entry_attributes_json, next_event_attributes_json "
                "FROM debug_unmatched_entries WHERE event_index = 1"
            ).fetchone()
        self.assertEqual(rows, [(3, 0), (5, 0), (6, 0), (8, 0), (9, 0)])
        self.assertEqual(debug_rows, [(1, "exit_link_mismatch", 2), (4, "exit_link_mismatch", 5),
                                     (7, "next_entry_before_exit", 8), (10, "end_of_file", None)])
        self.assertEqual(Path(source), self.events_directory / "city.xml")
        self.assertEqual(json.loads(entry_json), {"time": "1", "type": "entered link", "vehicle": "one",
                                                 "link": "road", "person": "p&x"})
        self.assertEqual(json.loads(next_json)["link"], "unknown")

    def test_traffic_times_use_recorded_endpoint_positions(self):
        (self.events_directory / "city.xml").write_text(
            '''<events>
            <event time="1" type="vehicle enters traffic" vehicle="one" link="road" relativePosition="0"/>
            <event time="2" type="left link" vehicle="one" link="road"/>
            <event time="3" type="entered link" vehicle="one" link="road"/>
            <event time="4" type="vehicle leaves traffic" vehicle="one" link="road" relativePosition="1"/>
            <event time="5" type="vehicle enters traffic" vehicle="one" link="road" relativePosition="0.5"/>
            <event time="6" type="left link" vehicle="one" link="road"/>
            <event time="7" type="entered link" vehicle="one" link="road"/>
            <event time="8" type="vehicle leaves traffic" vehicle="one" link="road" relativePosition="0.5"/>
            <event time="9" type="left link" vehicle="one" link="road"/>
            <event time="10" type="vehicle enters traffic" vehicle="one" link="road"/>
            <event time="11" type="vehicle leaves traffic" vehicle="one" link="road"/>
            <event time="12" type="vehicle leaves traffic" vehicle="one" link="road" relativePosition="0"/>
            </events>''', encoding="utf-8",
        )
        self.run_import()
        with closing(sqlite3.connect(self.output_directory / "city.sqlite")) as connection:
            rows = {row[1]: row for row in connection.execute("SELECT * FROM agent_events")}
            self.assertEqual(connection.execute("SELECT count(*) FROM debug_unmatched_entries").fetchone(), (0,))
        self.assertEqual(set(rows), {1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 12})
        start = importer.wgs84_to_ecef(16, 59)
        end = importer.wgs84_to_ecef(17, 60)
        midpoint = tuple((a + b) / 2 for a, b in zip(start, end))
        for time, expected in ((1, start), (2, end), (5, midpoint), (6, end), (8, midpoint), (12, start)):
            for actual, coordinate in zip(rows[time][4:7], expected):
                self.assertAlmostEqual(actual, coordinate)
        for entry_time, exit_time in ((1, 2), (3, 4), (5, 6), (7, 8)):
            for axis in range(3):
                self.assertAlmostEqual(rows[entry_time][4 + axis] + rows[entry_time][7 + axis]
                                       * rows[entry_time][10] * (exit_time - entry_time), rows[exit_time][4 + axis])
        self.assertEqual(rows[11][2], 4)
        self.assertEqual(rows[11][4:7], (None, None, None))

    def test_unknown_links_do_not_hide_unmatched_entries_or_arrivals(self):
        (self.events_directory / "city.xml").write_text(
            '''<events>
            <event time="1" type="entered link" vehicle="paired" link="unknown"/>
            <event time="2" type="left link" vehicle="paired" link="unknown"/>
            <event time="3" type="entered link" vehicle="unmatched" link="unknown"/>
            <event time="4" type="vehicle leaves traffic" vehicle="paired" link="unknown" relativePosition="1"/>
            <event time="5" type="entered link" link="road"/>
            </events>''', encoding="utf-8",
        )
        self.run_import()
        with closing(sqlite3.connect(self.output_directory / "city.sqlite")) as connection:
            debug_rows = connection.execute(
                "SELECT agent_id, link_id, reason FROM debug_unmatched_entries ORDER BY event_index"
            ).fetchall()
            rows = connection.execute("SELECT * FROM agent_events").fetchall()
        self.assertEqual(debug_rows, [("unmatched", "unknown", "end_of_file"), (None, "road", "missing_vehicle")])
        self.assertEqual(rows, [(4, 4, 4, "paired", None, None, None, 0, 0, 0, 0)])

    def test_debugging_can_be_removed_without_changing_motion_rows(self):
        path = self.write_events("city.xml", "one", 1)
        path.write_text(path.read_text(encoding="utf-8").replace(
            "</events>", '<event time="3" type="entered link" vehicle="one" link="road"/></events>'
        ), encoding="utf-8")
        self.run_import()
        with closing(sqlite3.connect(self.output_directory / "city.sqlite")) as connection:
            expected = connection.execute("SELECT * FROM agent_events ORDER BY agent_info_rowid").fetchall()
        with closing(sqlite3.connect(":memory:")) as connection:
            importer.create_schema(connection)
            importer.import_events(connection, path, importer.load_link_positions(self.network_path), 0, 50000)
            self.assertEqual(connection.execute("SELECT * FROM agent_events ORDER BY agent_info_rowid").fetchall(), expected)
            self.assertEqual(connection.execute("SELECT name FROM sqlite_master WHERE type = 'table'").fetchall(),
                             [("agent_events",)])

    def test_projected_network_crs_and_text_node_ids(self):
        try:
            import pyproj
        except ImportError:
            self.skipTest("pyproj is needed for projected networks")
        self.network_path.write_text(
            '<network><attributes><attribute name="coordinateReferenceSystem">EPSG:3857</attribute></attributes>'
            '<nodes><node id="west" x="0" y="0"/><node id="east" x="111.31949079327357" y="0"/></nodes>'
            '<links><link id="road" from="west" to="east"/></links></network>', encoding="utf-8",
        )
        endpoints = importer.load_link_positions(self.network_path)["road"]
        for position, expected in zip(endpoints, (importer.wgs84_to_ecef(0, 0), importer.wgs84_to_ecef(0.001, 0))):
            for actual, coordinate in zip(position, expected):
                self.assertAlmostEqual(actual, coordinate, places=6)
        overridden = importer.load_link_positions(self.network_path, "EPSG:4326")["road"][1]
        for actual, coordinate in zip(overridden, importer.wgs84_to_ecef(111.31949079327357, 0)):
            self.assertAlmostEqual(actual, coordinate, places=6)

    def test_defaults_use_simulator_folders(self):
        with patch.object(sys, "argv", [str(SCRIPT_PATH)]):
            args = importer.parse_args()
        self.assertEqual(args.events, [importer.SIMULATOR_ROOT / "input" / "event_files"])
        self.assertEqual(args.network, importer.SIMULATOR_ROOT / "input" / "network_file" / "network.xml")
        self.assertEqual(args.output, importer.SIMULATOR_ROOT / "output")

    def test_default_import_is_independent_of_working_directory(self):
        self.write_events("city.xml", "one", 1)
        with patch.object(importer, "DEFAULT_EVENTS_DIRECTORY", self.events_directory), \
                patch.object(importer, "DEFAULT_NETWORK_PATH", self.network_path), \
                patch.object(importer, "DEFAULT_OUTPUT_DIRECTORY", self.output_directory), \
                patch.object(sys, "argv", [str(SCRIPT_PATH)]), \
                patch("os.getcwd", return_value=str(self.events_directory)), redirect_stdout(io.StringIO()):
            importer.main()
        self.assertTrue((self.output_directory / "city.sqlite").is_file())
        self.assertFalse((self.events_directory / "output").exists())

    def test_explicit_files_and_network_in_events_folder(self):
        first = self.write_events("first.xml", "one", 1)
        second = self.write_events("second.xml", "two", 2)
        discovered = importer.discover_event_files([first, second, self.network_path], {self.network_path})
        self.assertEqual(discovered, [first, second])
        self.run_import(("--events", str(first), str(second)))
        self.assertEqual({path.name for path in self.output_directory.iterdir()}, {"first.sqlite", "second.sqlite"})

    def test_single_file_reimport_leaves_other_databases_and_event_files_untouched(self):
        first = self.write_events("first.xml", "one", 1)
        second = self.write_events("second.xml", "two", 2)
        self.run_import()
        other_database = self.output_directory / "second.sqlite"
        original_contents = other_database.read_bytes()

        self.write_events("first.xml", "replacement", 10)
        # Neither the malformed neighbor nor a newly added scenario should be read.
        second.write_text("<invalid", encoding="utf-8")
        self.write_events("third.xml", "three", 3)
        self.run_import(("--events", str(first)))

        self.assertEqual({path.name for path in self.output_directory.iterdir()},
                         {"first.sqlite", "second.sqlite"})
        self.assertEqual(other_database.read_bytes(), original_contents)
        with closing(sqlite3.connect(self.output_directory / "first.sqlite")) as connection:
            rows = connection.execute("SELECT agent_id, time FROM agent_events ORDER BY time").fetchall()
        self.assertEqual(rows, [("replacement", 10), ("replacement", 11)])

    def test_limit_is_applied_to_each_file(self):
        self.write_events("first.xml", "one", 1)
        self.write_events("second.xml", "two", 2)
        self.run_import(("--limit-events", "1"))
        for path, time in ((self.output_directory / "first.sqlite", 1),
                           (self.output_directory / "second.sqlite", 2)):
            with closing(sqlite3.connect(path)) as connection:
                self.assertEqual(connection.execute("SELECT count(*) FROM agent_events").fetchone(), (0,))
                self.assertEqual(connection.execute("SELECT time, reason FROM debug_unmatched_entries").fetchall(),
                                 [(time, "event_limit")])

    def test_missing_events_or_network_does_not_create_output(self):
        with self.assertRaisesRegex(FileNotFoundError, "No MATSim events XML"):
            self.run_import()
        self.write_events("city.xml", "one", 1)
        self.network_path.unlink()
        with self.assertRaisesRegex(FileNotFoundError, "network XML not found"):
            self.run_import()
        self.assertFalse(self.output_directory.exists())

    def test_duplicate_output_names_are_rejected_before_replacing_databases(self):
        first = self.write_events("city.xml", "one", 1)
        other_directory = self.root / "other_events"
        other_directory.mkdir()
        second = other_directory / "CITY.xml"
        second.write_text(first.read_text(encoding="utf-8"), encoding="utf-8")
        self.output_directory.mkdir()
        existing_database = self.output_directory / "city.sqlite"
        existing_database.write_bytes(b"keep existing database")
        with self.assertRaisesRegex(ValueError, "same output database"):
            self.run_import(("--events", str(first), str(second)))
        self.assertEqual(existing_database.read_bytes(), b"keep existing database")

    def test_missing_network_preserves_existing_database(self):
        self.write_events("city.xml", "one", 1)
        self.run_import()
        self.network_path.unlink()
        existing_database = self.output_directory / "city.sqlite"
        original_contents = existing_database.read_bytes()
        with self.assertRaisesRegex(FileNotFoundError, "network XML not found"):
            self.run_import()
        self.assertEqual(existing_database.read_bytes(), original_contents)

    def test_reimport_replaces_database_instead_of_appending(self):
        self.write_events("city.xml", "one", 1)
        self.run_import()
        self.write_events("city.xml", "two", 2)
        self.run_import()
        with closing(sqlite3.connect(self.output_directory / "city.sqlite")) as connection:
            rows = connection.execute("SELECT agent_id FROM agent_events").fetchall()
        self.assertEqual(rows, [("two",), ("two",)])


if __name__ == "__main__":
    unittest.main()

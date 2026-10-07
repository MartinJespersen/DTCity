import importlib.util
import io
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
            '<links><link id="road" from="1" to="2"/></links></network>',
            encoding="utf-8",
        )
        self.map_path = self.root / "input" / "map_file" / "roads.json"
        self.map_path.parent.mkdir()
        self.map_path.write_text("{}", encoding="utf-8")
        self.output_directory = self.root / "output"

    def write_events(self, name, vehicle, time):
        path = self.events_directory / name
        path.write_text(
            f'<events><event time="{time}" type="entered link" vehicle="{vehicle}" link="road"/>'
            f'<event time="{time + 1}" type="vehicle leaves traffic" vehicle="{vehicle}" link="road"/></events>',
            encoding="utf-8",
        )
        return path

    def run_import(self, extra_args=()):
        arguments = [str(SCRIPT_PATH), "--events", str(self.events_directory),
                     "--network", str(self.network_path), "--map", str(self.map_path.parent),
                     "--output", str(self.output_directory),
                     *extra_args]
        with patch.object(sys, "argv", arguments), redirect_stdout(io.StringIO()):
            importer.main()

    def test_multiple_files_have_separate_rows_and_matching_names(self):
        self.write_events("city.day 1.xml", "bus-1", 10)
        self.write_events("town.XML", "car-2", 20)
        (self.events_directory / "notes.txt").write_text("ignored", encoding="utf-8")
        with patch.object(importer, "load_link_nodes", wraps=importer.load_link_nodes) as load_network:
            self.run_import(("--batch-size", "1"))
        load_network.assert_called_once()
        self.assertEqual({path.name for path in self.output_directory.iterdir()},
                         {"city.day 1.sqlite", "town.sqlite"})
        for name, vehicle, time in (("city.day 1.sqlite", "bus-1", 10), ("town.sqlite", "car-2", 20)):
            with self.subTest(name=name), closing(sqlite3.connect(self.output_directory / name)) as connection:
                rows = connection.execute(
                    "SELECT agent_info_rowid, time, event_type, agent_id, node_from, node_to, "
                    "node_from_lon, node_from_lat, node_to_lon, node_to_lat FROM agent_events ORDER BY time"
                ).fetchall()
                self.assertEqual(rows, [(1, time, 1, vehicle, 1, 2, 16, 59, 17, 60),
                                        (2, time + 1, 4, vehicle, 1, 2, 16, 59, 17, 60)])

    def test_defaults_use_simulator_folders(self):
        with patch.object(sys, "argv", [str(SCRIPT_PATH)]):
            args = importer.parse_args()
        self.assertEqual(args.events, [importer.SIMULATOR_ROOT / "input" / "event_files"])
        self.assertEqual(args.network, importer.SIMULATOR_ROOT / "input" / "network_file" / "network.xml")
        self.assertEqual(args.map, importer.SIMULATOR_ROOT / "input" / "map_file")
        self.assertEqual(args.output, importer.SIMULATOR_ROOT / "output")

    def test_default_import_is_independent_of_working_directory(self):
        self.write_events("city.xml", "one", 1)
        with patch.object(importer, "DEFAULT_EVENTS_DIRECTORY", self.events_directory), \
                patch.object(importer, "DEFAULT_NETWORK_PATH", self.network_path), \
                patch.object(importer, "DEFAULT_MAP_PATH", self.map_path.parent), \
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

    def test_limit_is_applied_to_each_file(self):
        self.write_events("first.xml", "one", 1)
        self.write_events("second.xml", "two", 2)
        self.run_import(("--limit-events", "1"))
        for path in self.output_directory.iterdir():
            with closing(sqlite3.connect(path)) as connection:
                count = connection.execute("SELECT count(*) FROM agent_events").fetchone()[0]
                self.assertEqual(count, 1)

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

    def test_missing_map_preserves_existing_database(self):
        self.write_events("city.xml", "one", 1)
        self.run_import()
        self.map_path.unlink()
        existing_database = self.output_directory / "city.sqlite"
        original_contents = existing_database.read_bytes()
        with self.assertRaisesRegex(FileNotFoundError, "Map file not found"):
            self.run_import(("--map", str(self.map_path)))
        self.assertEqual(existing_database.read_bytes(), original_contents)

    def test_ambiguous_map_folder_requires_explicit_file(self):
        self.write_events("city.xml", "one", 1)
        (self.map_path.parent / "another.json").write_text("{}", encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "exactly one map file"):
            self.run_import()
        self.assertFalse(self.output_directory.exists())
        self.run_import(("--map", str(self.map_path)))
        self.assertTrue((self.output_directory / "city.sqlite").is_file())

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

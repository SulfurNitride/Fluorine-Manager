from __future__ import annotations

import importlib.util
import unittest
from pathlib import Path


MODULE_PATH = (
    Path(__file__).parents[2]
    / "libs"
    / "basic_games"
    / "games"
    / "openmw_support"
    / "process_lifetime.py"
)
SPEC = importlib.util.spec_from_file_location("openmw_process_lifetime", MODULE_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"Unable to load {MODULE_PATH}")
process_lifetime = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(process_lifetime)


class OpenMWProcessLifetimeTests(unittest.TestCase):
    def test_native_launcher_tracks_engine(self) -> None:
        self.assertEqual(
            process_lifetime.companion_process_names(
                "/usr/bin/openmw-launcher", [], "org.openmw.OpenMW"
            ),
            ["openmw"],
        )

    def test_flatpak_launcher_tracks_launcher_and_engine(self) -> None:
        self.assertEqual(
            process_lifetime.companion_process_names(
                "/usr/bin/flatpak",
                ["run", "--command=openmw-launcher", "org.openmw.OpenMW"],
                "org.openmw.OpenMW",
            ),
            ["openmw-launcher", "openmw"],
        )

    def test_flatpak_engine_tracks_engine(self) -> None:
        self.assertEqual(
            process_lifetime.companion_process_names(
                "/usr/bin/flatpak",
                ["run", "org.openmw.OpenMW"],
                "org.openmw.OpenMW",
            ),
            ["openmw"],
        )

    def test_unrelated_executable_has_no_companions(self) -> None:
        self.assertEqual(
            process_lifetime.companion_process_names(
                "/usr/bin/flatpak", ["run", "org.example.Game"], "org.openmw.OpenMW"
            ),
            [],
        )


if __name__ == "__main__":
    unittest.main()

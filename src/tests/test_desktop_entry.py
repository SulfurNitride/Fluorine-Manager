#!/usr/bin/env python3

import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest


MODULE_PATH = Path(__file__).resolve().parents[2] / "packaging" / "desktop_entry.py"
SPEC = importlib.util.spec_from_file_location("desktop_entry", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
desktop_entry = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = desktop_entry
SPEC.loader.exec_module(desktop_entry)


class DesktopEntryTests(unittest.TestCase):
    def test_escapes_every_reserved_exec_character(self):
        value = '/tmp/home space/"quote"/$cash/`tick`/100%/back\\slash'
        self.assertEqual(
            desktop_entry.escape_exec_argument(value),
            '"/tmp/home space/\\"quote\\"/\\$cash/\\`tick\\`/100%%/back\\\\slash"',
        )

    def test_rejects_control_characters(self):
        for value in ("nul\0path", "line\nfeed", "carriage\rreturn"):
            with self.subTest(value=value):
                with self.assertRaises(ValueError):
                    desktop_entry.escape_exec_argument(value)

    def test_renders_atomically_and_replaces_target_symlink(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "source.desktop"
            target = root / "installed/entry.desktop"
            victim = root / "victim"
            source.write_text(
                "[Desktop Entry]\nExec=fluorine-manager %u\nTerminal=false\n",
                encoding="utf-8",
            )
            target.parent.mkdir()
            victim.write_text("unchanged", encoding="utf-8")
            target.symlink_to(victim)

            desktop_entry.render_desktop_entry(
                source, target, Path('/tmp/home space/$100%/fluorine-manager')
            )

            self.assertFalse(target.is_symlink())
            self.assertEqual(victim.read_text(encoding="utf-8"), "unchanged")
            self.assertIn(
                'Exec="/tmp/home space/\\$100%%/fluorine-manager" %u',
                target.read_text(encoding="utf-8"),
            )
            self.assertEqual(target.stat().st_mode & 0o777, 0o755)

    def test_invalid_template_preserves_existing_target(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "source.desktop"
            target = root / "installed.desktop"
            source.write_text("[Desktop Entry]\nTerminal=false\n", encoding="utf-8")
            target.write_text("existing", encoding="utf-8")

            with self.assertRaises(ValueError):
                desktop_entry.render_desktop_entry(
                    source, target, Path("/tmp/fluorine-manager")
                )
            self.assertEqual(target.read_text(encoding="utf-8"), "existing")


if __name__ == "__main__":
    unittest.main()

#!/usr/bin/env python3

import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class NexusCacheSurfaceTests(unittest.TestCase):
    def test_clear_cache_never_deletes_the_configured_root(self):
        source = (ROOT / "src/src/settingsdialognexus.cpp").read_text(
            encoding="utf-8"
        )
        match = re.search(
            r"void NexusSettingsTab::clearCache\(\)\s*\{(?P<body>.*?)\n\}",
            source,
            re.DOTALL,
        )
        self.assertIsNotNone(match)
        body = match.group("body")
        self.assertIn("NexusInterface::instance().clearCache();", body)
        self.assertNotIn("removeRecursively", body)
        self.assertNotIn("settings().paths().cache", body.lower())

    def test_network_cache_uses_the_prepared_owned_child(self):
        source = (ROOT / "src/src/nexusinterface.cpp").read_text(encoding="utf-8")
        match = re.search(
            r"void NexusInterface::setCacheDirectory\([^)]*\)\s*"
            r"\{(?P<body>.*?)\n\}",
            source,
            re.DOTALL,
        )
        self.assertIsNotNone(match)
        body = match.group("body")
        self.assertIn("NexusCacheDirectory::configure(*m_AccessManager, directory)", body)
        self.assertNotIn("setCacheDirectory(directory)", body)
        self.assertNotIn("m_DiskCache", body)

    def test_tooltip_describes_the_bounded_operation(self):
        ui = (ROOT / "src/src/settingsdialog.ui").read_text(encoding="utf-8")
        self.assertIn(
            "Remove the Nexus network cache and Nexus cookies.",
            ui,
        )


if __name__ == "__main__":
    unittest.main()

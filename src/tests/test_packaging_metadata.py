#!/usr/bin/env python3

import configparser
from pathlib import Path
import unittest
import xml.etree.ElementTree as ET


SOURCE_ROOT = Path(__file__).resolve().parents[2]
DESKTOP_PATH = SOURCE_ROOT / "data/icons/com.fluorine.manager.desktop"
APPSTREAM_PATH = SOURCE_ROOT / "data/icons/com.fluorine.manager.metainfo.xml"


class PackagingMetadataTests(unittest.TestCase):
    def test_desktop_entry_has_one_supported_launcher(self):
        parser = configparser.ConfigParser(interpolation=None, strict=True)
        parser.optionxform = str
        with DESKTOP_PATH.open(encoding="utf-8") as stream:
            parser.read_file(stream)

        self.assertEqual(parser.sections(), ["Desktop Entry"])
        desktop = parser["Desktop Entry"]
        self.assertEqual(desktop["Type"], "Application")
        self.assertEqual(desktop["Exec"], "fluorine-manager %u")
        self.assertEqual(desktop["Icon"], "com.fluorine.manager")
        self.assertNotIn("Actions", desktop)

    def test_appstream_points_to_fluorine(self):
        component = ET.parse(APPSTREAM_PATH).getroot()
        self.assertEqual(component.tag, "component")
        self.assertEqual(component.findtext("id"), "com.fluorine.manager")
        self.assertEqual(
            component.findtext("launchable"), "com.fluorine.manager.desktop"
        )
        self.assertEqual(component.findtext("provides/binary"), "fluorine-manager")

        urls = {
            element.attrib["type"]: element.text
            for element in component.findall("url")
        }
        self.assertEqual(
            urls,
            {
                "homepage": "https://github.com/SulfurNitride/Fluorine-Manager",
                "bugtracker": (
                    "https://github.com/SulfurNitride/Fluorine-Manager/issues"
                ),
            },
        )

    def test_offline_package_documents_are_present(self):
        for relative_path in (
            "LICENSE.txt",
            "packaging/README-PORTABLE.txt",
            "packaging/THIRD-PARTY-NOTICES.txt",
            "docs/installation.md",
            "libs/dds-header/LICENSE",
        ):
            path = SOURCE_ROOT / relative_path
            self.assertTrue(path.is_file(), relative_path)
            self.assertTrue(path.read_text(encoding="utf-8").strip(), relative_path)

        dds_header = (
            SOURCE_ROOT / "libs/dds-header/include/DDS/DDS.h"
        ).read_text(encoding="utf-8")
        dds_license = (SOURCE_ROOT / "libs/dds-header/LICENSE").read_text(
            encoding="utf-8"
        )
        self.assertIn("Copyright (c) Microsoft Corporation", dds_header)
        self.assertIn("Copyright (c) Microsoft Corporation", dds_license)
        self.assertIn("Permission is hereby granted", dds_license)

    def test_stable_release_always_uses_versioned_notes(self):
        workflow = (SOURCE_ROOT / ".github/workflows/ci.yml").read_text(
            encoding="utf-8"
        )
        stable_step = workflow.split("- name: Publish stable release", 1)[1]
        self.assertIn('RELEASE_NOTES="docs/release-notes-${VERSION}.md"', stable_step)
        self.assertEqual(
            stable_step.count('--notes-file "${NOTES_FILE}"'),
            3,
            "draft edit, published edit, and create must use reviewed notes",
        )

    def test_portable_runtime_selects_the_bundled_native_dialog_portal(self):
        build_script = (SOURCE_ROOT / "docker/build-inner.sh").read_text(
            encoding="utf-8"
        )
        self.assertIn(
            'platformthemes/libqxdgdesktopportal.so', build_script
        )
        self.assertIn(
            'if [ ! -f "${PORTAL_THEME_PLUGIN}" ]', build_script
        )
        launcher = build_script.split(
            'cat > "${OUT_DIR}/fluorine-manager" <<\'LAUNCH\'\n', 1
        )[1].split("\nLAUNCH\n", 1)[0]
        self.assertIn(
            'FLUORINE_ORIG_QT_QPA_PLATFORMTHEME="${QT_QPA_PLATFORMTHEME:-}"',
            launcher,
        )
        self.assertIn(
            "export QT_QPA_PLATFORMTHEME=xdgdesktopportal", launcher
        )

        application = (SOURCE_ROOT / "src/src/moapplication.cpp").read_text(
            encoding="utf-8"
        )
        constructor = application.split(
            "MOApplication::MOApplication(int& argc, char** argv)", 1
        )[1]
        self.assertIn("FLUORINE_ORIG_QT_QPA_PLATFORMTHEME", constructor)
        self.assertIn('qunsetenv("QT_QPA_PLATFORMTHEME")', constructor)
        self.assertIn('qputenv("QT_QPA_PLATFORMTHEME", original)', constructor)


if __name__ == "__main__":
    unittest.main()

#!/usr/bin/env python3

from pathlib import Path
import unittest
import xml.etree.ElementTree as ET


SOURCE_ROOT = Path(__file__).resolve().parents[2]
SOURCE_DIR = SOURCE_ROOT / "src/src"


class CollectionSurfaceTests(unittest.TestCase):
    def test_dormant_collection_installer_is_retired(self) -> None:
        retired_files = (
            "collectiondownloaddialog.cpp",
            "collectiondownloaddialog.h",
            "collectiondownloaddialog.ui",
            "collectioninstaller.cpp",
            "collectioninstaller.h",
            "collectionmanifest.cpp",
            "collectionmanifest.h",
            "nexuscollections.cpp",
            "nexuscollections.h",
        )
        for name in retired_files:
            self.assertFalse((SOURCE_DIR / name).exists(), name)

    def test_instance_manager_has_no_collection_entrypoint(self) -> None:
        ui = ET.parse(SOURCE_DIR / "instancemanagerdialog.ui").getroot()
        self.assertIsNone(ui.find(".//widget[@name='downloadCollection']"))

        source = (SOURCE_DIR / "instancemanagerdialog.cpp").read_text(
            encoding="utf-8"
        )
        header = (SOURCE_DIR / "instancemanagerdialog.h").read_text(
            encoding="utf-8"
        )
        for retired in (
            "CollectionDownloadDialog",
            "NexusCollections",
            "collectiondownloaddialog.h",
            "nexuscollections.h",
            "downloadCollection",
        ):
            self.assertNotIn(retired, source + header)

    def test_repository_handler_still_rejects_collection_paths(self) -> None:
        parser = (SOURCE_DIR / "nxmrequest.cpp").read_text(encoding="utf-8")
        self.assertIn("isRepositoryLink(message)", parser)
        self.assertIn("return std::nullopt;", parser)

        test = (
            SOURCE_ROOT / "src/tests/test_nxmrequest.cpp"
        ).read_text(encoding="utf-8")
        self.assertIn("RejectsUnsupportedCollectionLinks", test)
        self.assertIn("nxm://game/collections/abc/revisions/1", test)
        self.assertIn("modl://game/collections/abc/revisions/1", test)


if __name__ == "__main__":
    unittest.main()

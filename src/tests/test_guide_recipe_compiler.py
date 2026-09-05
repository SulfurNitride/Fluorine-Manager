#!/usr/bin/env python3
import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = ROOT / "tools" / "guide_recipe_compiler" / "compile.py"
SPEC = importlib.util.spec_from_file_location("guide_recipe_compiler", MODULE_PATH)
compiler = importlib.util.module_from_spec(SPEC)
assert SPEC.loader
sys.modules[SPEC.name] = compiler
SPEC.loader.exec_module(compiler)
LOCK_PATH = ROOT / "tools" / "guide_recipe_compiler" / "lock.py"
LOCK_SPEC = importlib.util.spec_from_file_location("guide_recipe_lock", LOCK_PATH)
locker = importlib.util.module_from_spec(LOCK_SPEC)
assert LOCK_SPEC.loader
sys.modules[LOCK_SPEC.name] = locker
LOCK_SPEC.loader.exec_module(locker)


class GuideRecipeCompilerTests(unittest.TestCase):
    def test_extracts_exact_page_order_files_and_separator(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "guide.html").write_text("""
              <div class="card"><h3>Creating a Separator in MO2</h3>
                <li>Name the separator <strong>Utilities</strong>.</li></div>
              <div class="card"><h3 class="link-download">
                <a href="https://www.nexusmods.com/newvegas/mods/42">Example Mod</a></h3>
                <li><b>Main Files</b> - Main Archive</li>
                <li><b>Optional Files</b> - Settings INI</li></div>
            """, encoding="utf-8")
            artifacts, ordered = compiler.parse_pages(root, ["guide.html"])
            self.assertEqual([item["kind"] for item in ordered],
                             ["separator", "mod", "mod"])
            self.assertEqual(ordered[0]["name"], "Utilities")
            self.assertEqual([item["fileLabel"] for item in artifacts],
                             ["Main Archive", "Settings INI"])
            self.assertEqual(artifacts[0]["modId"], 42)

    def test_extracts_multiple_store_choices_from_one_card(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "guide.html").write_text("""
              <div class="card"><h3>Game Patchers</h3>
                <h3 class="link-download"><a href="https://www.nexusmods.com/newvegas/mods/62552">4GB Patcher</a></h3>
                <b>Main Files</b> - 4GB Patcher
                <h3 class="link-download"><a href="https://www.nexusmods.com/newvegas/mods/81281">Epic Games Patcher</a></h3>
                <b>Main Files</b> - Epic Games Patcher
              </div>
            """, encoding="utf-8")
            artifacts, _ = compiler.parse_pages(root, ["guide.html"])
            self.assertEqual([(item["modId"], item["fileLabel"]) for item in artifacts],
                             [(62552, "4GB Patcher"),
                              (81281, "Epic Games Patcher")])

    def test_nested_downloads_do_not_capture_following_parent_files(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "guide.html").write_text("""
              <div class="card"><h3 class="link-download">
                <a href="https://www.nexusmods.com/newvegas/mods/20">Parent</a></h3>
                <ul><li><a href="https://www.nexusmods.com/newvegas/mods/10">Dependency</a></li>
                  <ul><li><b>Main Files</b> - Dependency File</li></ul>
                  <li><b>Main Files</b> - Parent File</li>
                  <li><b>Optional Files</b> - Parent Preset</li></ul></div>
            """, encoding="utf-8")
            artifacts, _ = compiler.parse_pages(root, ["guide.html"])
            self.assertEqual([(item["modId"], item["fileLabel"]) for item in artifacts],
                             [(10, "Dependency File"), (20, "Parent File"),
                              (20, "Parent Preset")])

    def test_does_not_mix_maintainer_locks_into_scraped_manifest(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "guide.html").write_text("""
              <div class="card"><h3 class="link-download">
                <a href="https://www.nexusmods.com/newvegas/mods/67811">Faster Main Menu</a></h3>
                <li><b>Main Files</b> - Faster Main Menu</li></div>
            """, encoding="utf-8")
            artifacts, _ = compiler.parse_pages(root, ["guide.html"])
            self.assertEqual(artifacts[0]["fileId"], 0)

    def test_lock_resolves_normalized_nexus_file_label(self):
        artifact = {
            "id": "menu", "fileLabel": "Faster Main Menu",
            "fileCategory": "main files", "fileId": 0,
        }
        selected = locker.select_file(artifact, [{
            "file_id": 42, "name": "faster-main_menu",
            "category_id": 1, "category_name": "MAIN", "version": "1.0",
        }])
        self.assertEqual(selected["file_id"], 42)

    def test_catalog_hashes_are_current(self):
        catalog_path = ROOT / "src/src/resources/curated-guides/catalog.json"
        catalog = json.loads(catalog_path.read_text(encoding="utf-8"))
        import hashlib
        for entry in catalog["recipes"]:
            manifest = catalog_path.parent / entry["manifest"]
            lock = catalog_path.parent / entry["lock"]
            self.assertEqual(hashlib.sha256(manifest.read_bytes()).hexdigest(),
                             entry["manifestSha256"])
            self.assertEqual(hashlib.sha256(lock.read_bytes()).hexdigest(),
                             entry["lockSha256"])

    def test_checked_in_recipes_match_pinned_sources_when_available(self):
        source_root = Path("/tmp/fluorine-research.PzAr66")
        cases = [
            ("vnv-base", source_root / "Viva-New-Vegas",
             ROOT / "src/src/resources/curated-guides/vnv-base.json"),
            ("vnv-extended", source_root / "Viva-New-Vegas",
             ROOT / "src/src/resources/curated-guides/vnv-extended.json"),
            ("tbot-essentials", source_root / "The-Best-of-Times",
             ROOT / "src/src/resources/curated-guides/tbot-essentials.json"),
        ]
        for guide, source, output in cases:
            if not source.is_dir():
                continue
            expected = compiler.compile_recipe(compiler.SPECS[guide], source)
            actual = json.loads(output.read_text(encoding="utf-8"))
            self.assertEqual(actual, expected)


if __name__ == "__main__":
    unittest.main()

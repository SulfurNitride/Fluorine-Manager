#!/usr/bin/env python3

import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest


SOURCE_ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = SOURCE_ROOT / "docker/collect_cargo_licenses.py"
SPEC = importlib.util.spec_from_file_location("collect_cargo_licenses", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
collector = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = collector
SPEC.loader.exec_module(collector)


class CargoLicenseCollectionTests(unittest.TestCase):
    def test_collects_target_reachable_license_texts(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            count = collector.collect_manifest(
                SOURCE_ROOT / "libs/bsa_ffi/Cargo.toml", output
            )
            self.assertGreater(count, 20)
            names = {path.name for path in output.iterdir()}
            self.assertTrue(any(name.startswith("anyhow-") for name in names))
            self.assertTrue(any(name.startswith("ba2-") for name in names))
            self.assertFalse(any(name.startswith("wasip2-") for name in names))

    def test_sanitizes_destination_names(self):
        self.assertEqual(collector.safe_name("crate 1/2"), "crate_1_2")

    def test_preserves_nested_notices_for_known_malformed_crate(self):
        with tempfile.TemporaryDirectory() as temporary:
            package_root = Path(temporary)
            (package_root / "src").mkdir()
            zstd_notice = package_root / "src/LICENSE-ZSTD"
            decoder_notice = package_root / "src/decode.rs"
            zstd_notice.write_text("BSD notice", encoding="utf-8")
            decoder_notice.write_text("MIT decoder notice", encoding="utf-8")
            manifest = package_root / "Cargo.toml"
            manifest.write_text("[package]\n", encoding="utf-8")

            self.assertEqual(
                collector.package_license_files(
                    {
                        "name": "rust-zstd",
                        "version": "0.1.0",
                        "license": "BSD-3-Clause AND MIT",
                        "manifest_path": str(manifest),
                        "source": "registry+https://github.com/rust-lang/crates.io-index",
                    }
                ),
                [zstd_notice, decoder_notice],
            )


if __name__ == "__main__":
    unittest.main()

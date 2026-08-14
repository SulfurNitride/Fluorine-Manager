#!/usr/bin/env python3

import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest


MODULE_PATH = (
    Path(__file__).resolve().parents[2] / "packaging" / "elf_dependency_policy.py"
)
SPEC = importlib.util.spec_from_file_location("elf_dependency_policy", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
policy = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = policy
SPEC.loader.exec_module(policy)


class ElfDependencyPolicyTests(unittest.TestCase):
    def test_parses_resolved_missing_and_direct_dependencies(self):
        parsed = policy.parse_ldd_output(
            """
            libQt6Core.so.6 => /bundle/lib/libQt6Core.so.6 (0x1)
            libmissing.so.1 => not found
            /lib64/ld-linux-x86-64.so.2 (0x2)
            linux-vdso.so.1 (0x3)
            """
        )
        self.assertEqual(
            parsed,
            [
                policy.Dependency("libQt6Core.so.6", "/bundle/lib/libQt6Core.so.6"),
                policy.Dependency("libmissing.so.1", None),
                policy.Dependency("ld-linux-x86-64.so.2", "/lib64/ld-linux-x86-64.so.2"),
                policy.Dependency("linux-vdso.so.1", "/host/linux-vdso.so.1"),
            ],
        )

    def test_accepts_bundled_dependency_and_bundled_soname_symlink(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            library = root / "lib" / "libowned.so.1.2"
            library.parent.mkdir()
            library.write_bytes(b"owned")
            soname = library.with_name("libowned.so.1")
            soname.symlink_to(library.name)

            dependencies = [
                policy.Dependency("libowned.so.1", str(soname)),
                policy.Dependency("libowned.so.1.2", str(library)),
            ]
            self.assertEqual(
                policy.dependency_errors(root, root / "program", dependencies), []
            )

    def test_accepts_only_explicit_host_runtime(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            dependencies = [
                policy.Dependency("libc.so.6", "/usr/lib/libc.so.6"),
                policy.Dependency("libxcb-render.so.0", "/usr/lib/libxcb-render.so.0"),
            ]
            self.assertEqual(
                policy.dependency_errors(root, root / "program", dependencies), []
            )

    def test_rejects_unexpected_host_dependency(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            errors = policy.dependency_errors(
                root,
                root / "program",
                [policy.Dependency("libsurprise.so.1", "/usr/lib/libsurprise.so.1")],
            )
            self.assertEqual(len(errors), 1)
            self.assertIn("unexpected host dependency", errors[0])

    def test_requires_xcb_utilities_and_libcrypt_in_bundle(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            dependencies = [
                policy.Dependency("libxcb-icccm.so.4", "/usr/lib/libxcb-icccm.so.4"),
                policy.Dependency("libxcb-util.so.1", "/usr/lib/libxcb-util.so.1"),
                policy.Dependency("libcrypt.so.1", "/usr/lib/libcrypt.so.1"),
            ]
            errors = policy.dependency_errors(root, root / "program", dependencies)
            self.assertEqual(len(errors), len(dependencies))
            for dependency in dependencies:
                self.assertTrue(
                    any(dependency.soname in error for error in errors),
                    dependency.soname,
                )

    def test_rejects_host_runtime_staged_in_bundle(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            library_root = root / "lib"
            library_root.mkdir()
            (library_root / "libgbm.so.1").write_bytes(b"host runtime")
            (library_root / "libutil.so.1").write_bytes(b"host runtime")
            (library_root / "libowned.so.1").write_bytes(b"owned")

            errors = policy.bundled_host_runtime_errors(root)
            self.assertEqual(len(errors), 2)
            self.assertTrue(any("libgbm.so.1" in error for error in errors))
            self.assertTrue(any("libutil.so.1" in error for error in errors))

    def test_rejects_missing_dependency(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            errors = policy.dependency_errors(
                root,
                root / "program",
                [policy.Dependency("libmissing.so.1", None)],
            )
            self.assertEqual(len(errors), 1)
            self.assertIn("missing libmissing.so.1", errors[0])


if __name__ == "__main__":
    unittest.main()

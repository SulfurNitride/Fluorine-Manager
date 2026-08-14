from __future__ import annotations

import importlib.util
import fcntl
import json
import os
import signal
import stat
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path


SOURCE_ROOT = Path(__file__).resolve().parents[2]
PUBLISHER_PATH = SOURCE_ROOT / "packaging" / "fluorine_publisher.py"
SPEC = importlib.util.spec_from_file_location("fluorine_publisher", PUBLISHER_PATH)
assert SPEC is not None and SPEC.loader is not None
publisher = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = publisher
SPEC.loader.exec_module(publisher)


class PublisherTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="fluorine-publisher-test-")
        self.root = Path(self.temporary.name)
        self.data = self.root / "data"
        self.destination = self.data / "bin"

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def make_bundle(self, name: str, files: dict[str, bytes]) -> Path:
        bundle = self.root / name
        bundle.mkdir()
        for relative, contents in files.items():
            path = bundle / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(contents)
        launcher = bundle / "fluorine-manager"
        launcher.write_text("#!/usr/bin/env bash\nexit 0\n", encoding="utf-8")
        launcher.chmod(0o755)
        (bundle / "ModOrganizer-core").write_bytes(b"core")
        (bundle / "fluorine-publisher.py").write_bytes(PUBLISHER_PATH.read_bytes())
        publisher.build_manifest(bundle)
        return bundle

    def test_manifest_tracks_nested_files_modes_and_symlink_targets(self) -> None:
        bundle = self.make_bundle("bundle", {"plugins/tool.so": b"one"})
        tool = bundle / "plugins" / "tool.so"
        tool.chmod(0o755)
        link = bundle / "plugins" / "current.so"
        link.symlink_to("tool.so")
        first = publisher.build_manifest(bundle)

        entries, _ = publisher._load_manifest(bundle / publisher.MANIFEST_NAME)
        by_path = {entry["path"]: entry for entry in entries}
        self.assertEqual(by_path["plugins/tool.so"]["mode"], 0o755)
        self.assertEqual(by_path["plugins/current.so"]["target"], "tool.so")
        self.assertEqual(publisher.validate_bundle(bundle), first)

        link.unlink()
        link.symlink_to("../ModOrganizer-core")
        second = publisher.build_manifest(bundle)
        self.assertNotEqual(first, second)
        tool.write_bytes(b"tampered")
        with self.assertRaises(publisher.PublishError):
            publisher.validate_bundle(bundle)

    def test_legacy_preserves_custom_files_and_quarantines_known_tombstones(self) -> None:
        bundle = self.make_bundle("bundle", {"plugins/new.so": b"new"})
        (self.destination / "plugins").mkdir(parents=True)
        custom = self.destination / "plugins" / "CustomPlugin.so"
        custom.write_bytes(b"custom")
        replaced = self.destination / "plugins" / "new.so"
        replaced.write_bytes(b"user collision")
        old_fontconfig = self.destination / "lib" / "libfontconfig.so.1"
        old_fontconfig.parent.mkdir()
        old_fontconfig.write_bytes(b"obsolete bundled runtime")
        external = self.root / "external"
        external.mkdir()
        sentinel = external / "sentinel"
        sentinel.write_bytes(b"safe")
        (self.destination / "user-link").symlink_to(
            external, target_is_directory=True
        )
        (self.destination / publisher.LEGACY_MANIFEST_NAME).write_text(
            "plugins\nlib\nuser-link\n", encoding="utf-8"
        )

        publisher.publish(bundle, self.destination, self.data)

        self.assertEqual((self.destination / "plugins" / "new.so").read_bytes(), b"new")
        self.assertEqual(custom.read_bytes(), b"custom")
        self.assertTrue((self.destination / "user-link").is_symlink())
        quarantined = list(
            (self.data / "legacy-quarantine").glob("*/lib/libfontconfig.so.1")
        )
        self.assertEqual(len(quarantined), 1)
        self.assertEqual(quarantined[0].read_bytes(), b"obsolete bundled runtime")
        preserved_collision = list(
            (self.data / "legacy-quarantine").glob("*/replaced/plugins/new.so")
        )
        self.assertEqual(len(preserved_collision), 1)
        self.assertEqual(preserved_collision[0].read_bytes(), b"user collision")
        self.assertEqual(sentinel.read_bytes(), b"safe")

    def test_bootstrap_preserves_legacy_and_modified_v2_launchers(self) -> None:
        legacy_bundle = self.make_bundle("legacy-new", {})
        self.destination.mkdir(parents=True)
        legacy_launcher = self.destination / "fluorine-manager"
        legacy_launcher.write_bytes(b"#!/bin/sh\necho custom legacy\n")
        legacy_launcher.chmod(0o755)
        (self.destination / publisher.LEGACY_MANIFEST_NAME).write_text(
            "fluorine-manager\n", encoding="utf-8"
        )
        publisher.publish(legacy_bundle, self.destination, self.data)
        preserved_legacy = list(
            (self.data / "legacy-quarantine").glob(
                "*/replaced/fluorine-manager"
            )
        )
        self.assertEqual(len(preserved_legacy), 1)
        self.assertIn(b"custom legacy", preserved_legacy[0].read_bytes())

        modified = self.destination / "fluorine-manager"
        modified.write_bytes(b"#!/bin/sh\necho custom v2\n")
        modified.chmod(0o755)
        replacement = self.make_bundle("v2-new", {"plugins/new.so": b"new"})
        publisher.publish(replacement, self.destination, self.data)
        preserved_v2 = list(
            (self.data / "legacy-quarantine").glob(
                "*/replaced/fluorine-manager"
            )
        )
        self.assertEqual(len(preserved_v2), 2)
        self.assertTrue(
            any(b"custom v2" in path.read_bytes() for path in preserved_v2)
        )

    def test_v2_removes_only_stale_owned_leaves_and_preserves_user_files(self) -> None:
        first = self.make_bundle(
            "first", {"plugins/keep.so": b"old", "plugins/stale.so": b"stale"}
        )
        publisher.publish(first, self.destination, self.data)
        custom = self.destination / "plugins" / "CustomPlugin.so"
        custom.write_bytes(b"custom")
        second = self.make_bundle("second", {"plugins/keep.so": b"new"})

        publisher.publish(second, self.destination, self.data)

        self.assertEqual((self.destination / "plugins" / "keep.so").read_bytes(), b"new")
        self.assertFalse((self.destination / "plugins" / "stale.so").exists())
        self.assertEqual(custom.read_bytes(), b"custom")

    def test_modified_retired_v2_leaf_is_preserved(self) -> None:
        first = self.make_bundle("first", {"plugins/retired.so": b"release"})
        publisher.publish(first, self.destination, self.data)
        retired = self.destination / "plugins" / "retired.so"
        retired.write_bytes(b"user modified")
        second = self.make_bundle("second", {})

        publisher.publish(second, self.destination, self.data)

        self.assertFalse(retired.exists())
        preserved = list(
            (self.data / "legacy-quarantine").glob(
                "*/modified-v2/plugins/retired.so"
            )
        )
        self.assertEqual(len(preserved), 1)
        self.assertEqual(preserved[0].read_bytes(), b"user modified")

    def test_already_missing_stale_parent_is_not_a_recovery_blocker(self) -> None:
        first = self.make_bundle("first", {"plugins/stale.so": b"stale"})
        publisher.publish(first, self.destination, self.data)
        stale = self.destination / "plugins" / "stale.so"
        stale.unlink()
        stale.parent.rmdir()
        second = self.make_bundle("second", {})

        bundle_id = publisher.publish(second, self.destination, self.data)

        self.assertEqual(
            publisher.check_installed(self.destination, self.data), bundle_id
        )

    def test_interrupted_transactions_resume_and_publish_marker_last(self) -> None:
        bundle = self.make_bundle("bundle", {"plugins/tool.so": b"tool"})
        failpoints = (
            "after-journal",
            "after-stale-backup",
            "after-entry:plugins/tool.so",
            "after-manifest",
            "after-marker",
        )
        for failpoint in failpoints:
            with self.subTest(failpoint=failpoint):
                case_data = self.root / failpoint.replace(":", "-")
                destination = case_data / "bin"
                old = os.environ.get("FLUORINE_PUBLISH_FAILPOINT")
                os.environ["FLUORINE_PUBLISH_FAILPOINT"] = failpoint
                try:
                    with self.assertRaises(publisher.PublishError):
                        publisher.publish(bundle, destination, case_data)
                finally:
                    if old is None:
                        os.environ.pop("FLUORINE_PUBLISH_FAILPOINT", None)
                    else:
                        os.environ["FLUORINE_PUBLISH_FAILPOINT"] = old
                self.assertTrue((case_data / "publish-transaction").exists())
                bundle_id = publisher.check_installed(destination, case_data)
                self.assertEqual(
                    (destination / publisher.MARKER_NAME).read_text(encoding="ascii"),
                    f"manifest:{bundle_id}\n",
                )
                self.assertFalse((case_data / "publish-transaction").exists())

    def test_prepared_replacement_keeps_old_leaf_until_atomic_replace(self) -> None:
        first = self.make_bundle("first", {"plugins/tool.so": b"old"})
        publisher.publish(first, self.destination, self.data)
        second = self.make_bundle("second", {"plugins/tool.so": b"new"})

        old = os.environ.get("FLUORINE_PUBLISH_FAILPOINT")
        os.environ["FLUORINE_PUBLISH_FAILPOINT"] = (
            "before-replace:plugins/tool.so"
        )
        try:
            with self.assertRaises(publisher.PublishError):
                publisher.publish(second, self.destination, self.data)
        finally:
            if old is None:
                os.environ.pop("FLUORINE_PUBLISH_FAILPOINT", None)
            else:
                os.environ["FLUORINE_PUBLISH_FAILPOINT"] = old

        self.assertEqual(
            (self.destination / "plugins" / "tool.so").read_bytes(), b"old"
        )
        recovery = publisher._recovery_stage(self.data)
        self.assertIsNotNone(recovery)
        assert recovery is not None
        self.assertTrue((recovery[0] / "fluorine-publisher.py").is_file())
        recovered_id = publisher.publish(recovery[0], self.destination, self.data)
        self.assertTrue(recovered_id)
        self.assertFalse(recovery[0].exists())
        self.assertEqual(
            (self.destination / "plugins" / "tool.so").read_bytes(), b"new"
        )

    def test_process_death_during_launcher_bootstrap_recovers_from_stage(self) -> None:
        bundle = self.make_bundle("bundle", {"plugins/tool.so": b"tool"})
        environment = os.environ.copy()
        environment["FLUORINE_PUBLISH_KILLPOINT"] = (
            "before-replace:fluorine-manager"
        )
        result = subprocess.run(
            [
                sys.executable,
                str(PUBLISHER_PATH),
                "publish",
                str(bundle),
                str(self.destination),
                str(self.data),
            ],
            env=environment,
            check=False,
        )
        self.assertEqual(result.returncode, -signal.SIGKILL)
        self.assertTrue((self.data / publisher.RECOVERY_NAME).is_file())
        self.assertTrue((self.data / "publish-transaction").is_dir())
        self.assertFalse((self.destination / "fluorine-manager").exists())

        recovery = publisher._recovery_stage(self.data)
        self.assertIsNotNone(recovery)
        assert recovery is not None
        publisher.publish(recovery[0], self.destination, self.data)

        self.assertTrue((self.destination / "fluorine-manager").is_file())
        self.assertFalse((self.data / publisher.RECOVERY_NAME).exists())
        self.assertFalse((self.data / "publish-transaction").exists())
        self.assertFalse(
            any(self.destination.glob(".fluorine-manager.fluorine-*"))
        )

    def test_managed_file_directory_transitions_publish_forward(self) -> None:
        first = self.make_bundle(
            "first", {"as-directory": b"old-file", "as-file/leaf": b"old-leaf"}
        )
        publisher.publish(first, self.destination, self.data)
        second = self.make_bundle(
            "second", {"as-directory/leaf": b"new-leaf", "as-file": b"new-file"}
        )

        publisher.publish(second, self.destination, self.data)

        self.assertEqual(
            (self.destination / "as-directory" / "leaf").read_bytes(), b"new-leaf"
        )
        self.assertEqual((self.destination / "as-file").read_bytes(), b"new-file")

    def test_topology_transition_recovers_after_stale_retirement(self) -> None:
        first = self.make_bundle(
            "first", {"as-directory": b"old-file", "as-file/leaf": b"old-leaf"}
        )
        publisher.publish(first, self.destination, self.data)
        second = self.make_bundle(
            "second", {"as-directory/leaf": b"new-leaf", "as-file": b"new-file"}
        )
        old = os.environ.get("FLUORINE_PUBLISH_FAILPOINT")
        os.environ["FLUORINE_PUBLISH_FAILPOINT"] = "after-stale-backup"
        try:
            with self.assertRaises(publisher.PublishError):
                publisher.publish(second, self.destination, self.data)
        finally:
            if old is None:
                os.environ.pop("FLUORINE_PUBLISH_FAILPOINT", None)
            else:
                os.environ["FLUORINE_PUBLISH_FAILPOINT"] = old

        recovery = publisher._recovery_stage(self.data)
        self.assertIsNotNone(recovery)
        assert recovery is not None
        publisher.publish(recovery[0], self.destination, self.data)
        self.assertEqual(
            (self.destination / "as-directory" / "leaf").read_bytes(), b"new-leaf"
        )
        self.assertEqual((self.destination / "as-file").read_bytes(), b"new-file")

    def test_unowned_collision_and_symlinked_stale_parent_fail_closed(self) -> None:
        first = self.make_bundle("first", {"plugins/stale.so": b"old"})
        old_id = publisher.publish(first, self.destination, self.data)
        old_launcher = (self.destination / "fluorine-manager").read_bytes()
        custom = self.destination / "plugins" / "new.so"
        custom.write_bytes(b"custom")
        second = self.make_bundle("second", {"plugins/new.so": b"release"})
        with self.assertRaisesRegex(publisher.PublishError, "unowned path"):
            publisher.publish(second, self.destination, self.data)
        self.assertEqual(custom.read_bytes(), b"custom")
        self.assertEqual(
            (self.destination / "plugins" / "stale.so").read_bytes(), b"old"
        )
        self.assertEqual(
            (self.destination / "fluorine-manager").read_bytes(), old_launcher
        )
        self.assertFalse((self.data / publisher.RECOVERY_NAME).exists())
        self.assertFalse((self.data / "publish-transaction").exists())
        self.assertEqual(
            publisher.check_installed(self.destination, self.data), old_id
        )

        other_data = self.root / "symlink-data"
        other_destination = other_data / "bin"
        publisher.publish(first, other_destination, other_data)
        old_other_id = publisher.check_installed(other_destination, other_data)
        shutil_path = other_destination / "plugins"
        shutil_path.rename(other_destination / "plugins-owned")
        external = self.root / "external-stale"
        external.mkdir()
        external_stale = external / "stale.so"
        external_stale.write_bytes(b"outside")
        shutil_path.symlink_to(external, target_is_directory=True)
        empty = self.make_bundle("empty", {})
        with self.assertRaisesRegex(publisher.PublishError, "non-directory parent"):
            publisher.publish(empty, other_destination, other_data)
        self.assertEqual(external_stale.read_bytes(), b"outside")
        self.assertFalse((other_data / publisher.RECOVERY_NAME).exists())
        self.assertFalse((other_data / "publish-transaction").exists())
        self.assertEqual(
            publisher.check_installed(other_destination, other_data), old_other_id
        )

    def test_lock_serializes_publishers(self) -> None:
        bundle = self.make_bundle("bundle", {"plugins/tool.so": b"tool"})
        with publisher.PublicationLock(self.data):
            process = subprocess.Popen(
                [
                    sys.executable,
                    str(PUBLISHER_PATH),
                    "publish",
                    str(bundle),
                    str(self.destination),
                    str(self.data),
                ],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )
            time.sleep(0.2)
            self.assertIsNone(process.poll())
        stdout, stderr = process.communicate(timeout=10)
        self.assertEqual(process.returncode, 0, stderr.decode())
        self.assertTrue(stdout.strip())

    def test_running_generation_blocks_only_mutating_publication(self) -> None:
        first = self.make_bundle("first", {"plugins/tool.so": b"old"})
        publisher.publish(first, self.destination, self.data)
        second = self.make_bundle("second", {"plugins/tool.so": b"new"})

        runtime_lock = (self.data / "runtime.lock").open("a+b")
        fcntl.flock(runtime_lock.fileno(), fcntl.LOCK_SH)
        try:
            self.assertTrue(publisher.check_installed(self.destination, self.data))
            with self.assertRaisesRegex(publisher.PublishError, "is running"):
                publisher.publish(second, self.destination, self.data)
            self.assertEqual(
                (self.destination / "plugins" / "tool.so").read_bytes(), b"old"
            )
            self.assertFalse((self.data / "publish-transaction").exists())
            stages = self.data / "publish-stage"
            self.assertFalse(stages.exists() and any(stages.iterdir()))
        finally:
            fcntl.flock(runtime_lock.fileno(), fcntl.LOCK_UN)
            runtime_lock.close()

        publisher.publish(second, self.destination, self.data)
        self.assertEqual(
            (self.destination / "plugins" / "tool.so").read_bytes(), b"new"
        )

    def test_update_waits_for_runtime_lock_release(self) -> None:
        first = self.make_bundle("first", {"plugins/tool.so": b"old"})
        publisher.publish(first, self.destination, self.data)
        second = self.make_bundle("second", {"plugins/tool.so": b"new"})
        ready_read, ready_write = os.pipe()
        child = os.fork()
        if child == 0:
            os.close(ready_read)
            runtime_lock = (self.data / "runtime.lock").open("a+b")
            fcntl.flock(runtime_lock.fileno(), fcntl.LOCK_SH)
            os.write(ready_write, b"1")
            time.sleep(0.2)
            os._exit(0)
        os.close(ready_write)
        try:
            self.assertEqual(os.read(ready_read, 1), b"1")
            publisher.publish(second, self.destination, self.data, wait_runtime=2)
        finally:
            os.close(ready_read)
            os.waitpid(child, 0)
        self.assertEqual(
            (self.destination / "plugins" / "tool.so").read_bytes(), b"new"
        )

    def test_unsafe_or_unowned_inputs_fail_before_mutation(self) -> None:
        bundle = self.make_bundle("bundle", {"plugins/tool.so": b"tool"})
        manifest_path = bundle / publisher.MANIFEST_NAME
        document = json.loads(manifest_path.read_bytes())
        document["entries"][0]["path"] = "../escape"
        manifest_path.write_text(json.dumps(document), encoding="utf-8")
        with self.assertRaises(publisher.PublishError):
            publisher.publish(bundle, self.destination, self.data)
        self.assertFalse(self.destination.exists())

        existing = self.root / "unmanifested"
        existing.mkdir()
        (existing / "user-file").write_bytes(b"preserve")
        good = self.make_bundle("good", {"plugins/tool.so": b"tool"})
        with self.assertRaises(publisher.PublishError):
            publisher.publish(good, existing, self.root / "other-data")
        self.assertEqual((existing / "user-file").read_bytes(), b"preserve")

    def test_marker_mismatch_blocks_direct_installed_launch(self) -> None:
        bundle = self.make_bundle("bundle", {"plugins/tool.so": b"tool"})
        publisher.publish(bundle, self.destination, self.data)
        (self.destination / publisher.MARKER_NAME).write_text(
            "manifest:not-the-bundle\n", encoding="ascii"
        )
        with self.assertRaises(publisher.PublishError):
            publisher.check_installed(self.destination, self.data)
        replacement = self.make_bundle(
            "replacement", {"plugins/tool.so": b"replacement"}
        )
        with self.assertRaisesRegex(publisher.PublishError, "commit marker"):
            publisher.publish(replacement, self.destination, self.data)
        self.assertFalse((self.data / "publish-transaction").exists())
        self.assertFalse(any(self.data.glob(".publish-transaction-*")))
        self.assertEqual(
            (self.destination / "plugins" / "tool.so").read_bytes(), b"tool"
        )

    def test_abandoned_publication_stages_are_collected(self) -> None:
        bundle = self.make_bundle("bundle", {"plugins/tool.so": b"tool"})
        publisher.publish(bundle, self.destination, self.data)
        stage_root = self.data / "publish-stage"
        incoming = stage_root / ".incoming-abandoned"
        incoming.mkdir(parents=True)
        (incoming / "large-partial").write_bytes(b"partial")
        unreferenced = stage_root / ("a" * 64)
        unreferenced.mkdir()
        (unreferenced / "large-stage").write_bytes(b"staged")
        retired = stage_root / f".{('b' * 64)}.retired-deadbeef"
        retired.mkdir()
        (retired / "large-retired-stage").write_bytes(b"retired")

        publisher.check_installed(self.destination, self.data)

        self.assertFalse(incoming.exists())
        self.assertFalse(unreferenced.exists())
        self.assertFalse(retired.exists())
        publisher.check_installed(self.destination, self.data)
        self.assertFalse(any(stage_root.glob(".*.retired-*")))

    def test_launcher_execs_core_in_place_and_holds_runtime_lease(self) -> None:
        build_script = (SOURCE_ROOT / "docker" / "build-inner.sh").read_text(
            encoding="utf-8"
        )
        launcher = build_script.split(
            'cat > "${OUT_DIR}/fluorine-manager" <<\'LAUNCH\'\n', 1
        )[1].split("\nLAUNCH\n", 1)[0]
        self.assertNotIn("exec flock --shared", launcher)
        self.assertNotIn("--fluorine-run-locked", launcher)

        bundle = self.root / "launcher-bundle"
        bundle.mkdir()
        launcher_path = bundle / "fluorine-manager"
        launcher_path.write_text(launcher, encoding="utf-8")
        launcher_path.chmod(0o755)
        (bundle / "fluorine-publisher.py").write_bytes(PUBLISHER_PATH.read_bytes())

        python_wrapper = bundle / "python" / "bin" / "python3"
        python_wrapper.parent.mkdir(parents=True)
        python_wrapper.write_text(
            "#!/usr/bin/env bash\nunset PYTHONHOME\n"
            f'exec "{sys.executable}" "$@"\n',
            encoding="utf-8",
        )
        python_wrapper.chmod(0o755)

        core = bundle / "ModOrganizer-core"
        core.write_text(
            "#!/usr/bin/env python3\n"
            "import json, os, signal, time\n"
            "marker = os.environ['FLUORINE_TEST_CORE_MARKER']\n"
            "fd = int(os.environ['FLUORINE_RUNTIME_LOCK_FD'])\n"
            "os.fstat(fd)\n"
            "with open(marker, 'w', encoding='utf-8') as stream:\n"
            "    json.dump({'pid': os.getpid(), 'fd': fd}, stream)\n"
            "def stop(sig, frame):\n"
            "    with open(marker + '.signal', 'w', encoding='ascii') as stream:\n"
            "        stream.write(str(sig))\n"
            "    raise SystemExit(128 + sig)\n"
            "signal.signal(signal.SIGTERM, stop)\n"
            "while True:\n"
            "    time.sleep(0.05)\n",
            encoding="utf-8",
        )
        core.chmod(0o755)
        publisher.build_manifest(bundle)

        home = self.root / "launcher-home"
        home.mkdir()
        marker = self.root / "core-marker.json"
        environment = os.environ.copy()
        environment.update(
            {
                "HOME": str(home),
                "FLUORINE_TEST_CORE_MARKER": str(marker),
            }
        )
        process = subprocess.Popen(
            [str(launcher_path)],
            env=environment,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
            text=True,
        )
        try:
            deadline = time.monotonic() + 10
            while not marker.exists() and process.poll() is None:
                if time.monotonic() >= deadline:
                    self.fail("generated launcher did not exec the fake core")
                time.sleep(0.02)
            if process.poll() is not None:
                self.fail(
                    "generated launcher exited before the fake core: "
                    + process.stderr.read()
                )
            observed = json.loads(marker.read_text(encoding="utf-8"))
            self.assertEqual(observed["pid"], process.pid)

            runtime_path = home / ".local" / "share" / "fluorine" / "runtime.lock"
            with runtime_path.open("a+b") as competitor:
                with self.assertRaises(BlockingIOError):
                    fcntl.flock(competitor.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)

                process.send_signal(signal.SIGTERM)
                self.assertEqual(process.wait(timeout=5), 128 + signal.SIGTERM)
                self.assertEqual(
                    Path(str(marker) + ".signal").read_text(encoding="ascii"),
                    str(signal.SIGTERM),
                )
                fcntl.flock(competitor.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        finally:
            if process.poll() is None:
                process.kill()
                process.wait(timeout=5)
            if process.stderr is not None:
                process.stderr.close()


if __name__ == "__main__":
    unittest.main()

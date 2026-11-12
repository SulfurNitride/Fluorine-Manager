"""Exercise the generated launcher using a fake bundle and an isolated data root."""

import os
from pathlib import Path
import shutil
import shlex
import subprocess
import tarfile
import tempfile
import unittest


class BundleLauncherTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="fluorine-launcher-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.test_home = self.root / "home"
        self.installed = self.test_home / ".local/share/fluorine/bin"
        self.bundle = self.root / "bundle"
        source = (Path(__file__).resolve().parents[2] / "docker/build-inner.sh").read_text()
        launcher = source.split("<<'LAUNCH'\n", 1)[1].split("\nLAUNCH", 1)[0]
        # Redirect only the home-directory references; never touch the real install.
        launcher = launcher.replace("${HOME}", "${FLUORINE_TEST_HOME}")
        self.bundle.mkdir()
        (self.bundle / "lib").mkdir()
        (self.bundle / "lib/current-library.txt").write_text("current")
        self.write_executable(self.bundle / "fluorine-manager", launcher)
        self.write_executable(
            self.bundle / "ModOrganizer-core",
            '#!/usr/bin/env bash\nprintf "started" > "$FLUORINE_TEST_CAPTURE"\n',
        )
        (self.bundle / "fluorine-bundle-version.txt").write_text("test-bundle-1")
        (self.bundle / "fluorine-manifest.txt").write_text(
            "ModOrganizer-core\nfluorine-manager\nlib\n"
            "fluorine-bundle-version.txt\nfluorine-manifest.txt\n"
        )
        self.env = os.environ.copy()
        self.env.pop("XDG_DATA_HOME", None)
        self.env.update(
            FLUORINE_TEST_HOME=str(self.test_home),
            FLUORINE_TEST_CAPTURE=str(self.root / "started"),
            FLUORINE_DISABLE_FONTCONFIG_FIX="1",
        )

    @staticmethod
    def write_executable(path, text):
        path.write_text(text)
        path.chmod(0o755)

    def launch(self, path):
        result = subprocess.run(
            [str(path)], env=self.env, capture_output=True, text=True, timeout=15
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((self.root / "started").read_text(), "started")

    def seed_legacy_files(self):
        lib = self.installed / "lib"
        lib.mkdir(parents=True, exist_ok=True)
        for name in ["libgbm.so.1", "libssl.so.3", "libcrypto.so.3",
                     "libnss3.so", "libnssutil3.so", "libsmime3.so", "libssl3.so",
                     "libnspr4.so", "libplc4.so", "libplds4.so", "libsoftokn3.so",
                     "libfreebl3.so", "libfreeblpriv3.so", "libnssckbi.so",
                     "libnssdbm3.so", "libfontconfig.so.1.12.0"]:
            (lib / name).write_text("obsolete bundled runtime")
        (lib / "libgbm.so").symlink_to("libgbm.so.1")
        (lib / "libfontconfig.so.1").symlink_to("libfontconfig.so.1.12.0")
        (lib / "custom-plugin.so").write_text("user file")
        for arch in ["x86_64", "i386"]:
            locale = self.installed / "locale" / arch
            locale.mkdir(parents=True, exist_ok=True)
            (locale / "libfluorine_locale.so").write_text("retired interposer")
            (locale / "custom-file").write_text("user file")

    def assert_clean(self):
        lib = self.installed / "lib"
        for pattern in ["libgbm.so*", "libssl.so*", "libcrypto.so*",
                        "libnss*.so*", "libsmime3.so*", "libssl3.so*", "libnspr4.so*",
                        "libplc4.so*", "libplds4.so*", "libsoftokn3.so*",
                        "libfreebl*.so*", "libfontconfig.so*"]:
            self.assertEqual(list(lib.glob(pattern)), [])
        self.assertEqual((lib / "custom-plugin.so").read_text(), "user file")
        for arch in ["x86_64", "i386"]:
            locale = self.installed / "locale" / arch
            self.assertFalse((locale / "libfluorine_locale.so").exists())
            self.assertEqual((locale / "custom-file").read_text(), "user file")

    def test_upgrade_removes_retired_libraries_and_preserves_user_files(self):
        self.seed_legacy_files()
        self.launch(self.bundle / "fluorine-manager")
        self.assert_clean()

    def test_same_bundle_version_still_cleans_legacy_libraries(self):
        self.launch(self.bundle / "fluorine-manager")
        self.seed_legacy_files()
        self.launch(self.bundle / "fluorine-manager")
        self.assert_clean()

    def test_launching_installed_copy_still_cleans_legacy_libraries(self):
        self.launch(self.bundle / "fluorine-manager")
        self.seed_legacy_files()
        self.launch(self.installed / "fluorine-manager")
        self.assert_clean()

    def test_custom_data_home_installs_bundle_icons_and_desktop_entry(self):
        data = self.root / "custom data"
        self.env["XDG_DATA_HOME"] = str(data)
        self.installed = data / "fluorine/bin"
        icons = self.bundle / "icons"
        icons.mkdir()
        (icons / "com.fluorine.manager.png").write_bytes(b"test icon")
        (icons / "com.fluorine.manager.desktop").write_text(
            "[Desktop Entry]\nType=Application\nExec=fluorine-manager %u\n"
        )
        with (self.bundle / "fluorine-manifest.txt").open("a") as manifest:
            manifest.write("icons\n")
        self.seed_legacy_files()
        self.launch(self.bundle / "fluorine-manager")
        self.assert_clean()
        desktop = data / "applications/com.fluorine.manager.desktop"
        self.assertIn(f'Exec="{self.installed}/fluorine-manager" %u', desktop.read_text())
        self.assertTrue((data / "icons/hicolor/256x256/apps/com.fluorine.manager.png").exists())
        self.assertFalse((self.test_home / ".local/share").exists())
        self.launch(self.installed / "fluorine-manager")

        (icons / "com.fluorine.manager.png").write_bytes(b"upgraded icon")
        (self.bundle / "fluorine-bundle-version.txt").write_text("test-bundle-2")
        self.launch(self.bundle / "fluorine-manager")
        installed_icon = data / "icons/hicolor/256x256/apps/com.fluorine.manager.png"
        self.assertEqual(installed_icon.read_bytes(), b"upgraded icon")
        self.assertEqual(list(installed_icon.parent.glob("*.tmp.*")), [])

    def test_empty_and_relative_data_home_use_default(self):
        for value in ("", "relative/path"):
            with self.subTest(value=value):
                self.env["XDG_DATA_HOME"] = value
                self.launch(self.bundle / "fluorine-manager")
                self.assertTrue((self.installed / "ModOrganizer-core").exists())

    def test_file_picker_defaults_to_desktop_portal_and_preserves_override(self):
        platformthemes = self.bundle / "qt6plugins/platformthemes"
        platformthemes.mkdir(parents=True)
        (platformthemes / "libqxdgdesktopportal.so").touch()
        with (self.bundle / "fluorine-manifest.txt").open("a") as manifest:
            manifest.write("qt6plugins\n")
        self.write_executable(
            self.bundle / "ModOrganizer-core",
            '#!/usr/bin/env bash\nprintf "started" > "$FLUORINE_TEST_CAPTURE"\n'
            'printf "%s\\n%s" "${QT_QPA_PLATFORMTHEME:-}" '
            '"${FLUORINE_ORIG_QT_QPA_PLATFORMTHEME:-}" > "$FLUORINE_TEST_THEME_CAPTURE"\n',
        )
        capture = self.root / "theme"
        self.env["FLUORINE_TEST_THEME_CAPTURE"] = str(capture)
        self.env.pop("QT_QPA_PLATFORMTHEME", None)
        self.launch(self.bundle / "fluorine-manager")
        self.assertEqual(capture.read_text(), "xdgdesktopportal\n")

        self.env["QT_QPA_PLATFORMTHEME"] = "gtk3"
        self.launch(self.bundle / "fluorine-manager")
        self.assertEqual(capture.read_text(), "gtk3\ngtk3")

    def test_file_picker_without_portal_plugin_keeps_qt_fallback(self):
        self.write_executable(
            self.bundle / "ModOrganizer-core",
            '#!/usr/bin/env bash\nprintf "started" > "$FLUORINE_TEST_CAPTURE"\n'
            'printf "%s" "${QT_QPA_PLATFORMTHEME:-}" > "$FLUORINE_TEST_THEME_CAPTURE"\n',
        )
        capture = self.root / "theme"
        self.env["FLUORINE_TEST_THEME_CAPTURE"] = str(capture)
        self.env.pop("QT_QPA_PLATFORMTHEME", None)
        self.launch(self.bundle / "fluorine-manager")
        self.assertEqual(capture.read_text(), "")

    def test_installer_uses_same_data_root(self):
        source = (Path(__file__).resolve().parents[2] / "docker/build-inner.sh").read_text()
        header = source.split("<<'INSTALLER_HEADER'\n", 1)[1].split("\necho", 1)[0]
        for value in (None, "", "relative/path", str(self.root / "custom data")):
            with self.subTest(value=value):
                env = self.env.copy()
                env["HOME"] = str(self.test_home)
                if value is not None:
                    env["XDG_DATA_HOME"] = value
                script = header + '\nprintf "%s\\n" "$INSTALL_DIR" "$DESKTOP_DIR" "$ICON_DIR"\n'
                result = subprocess.run(["bash", "-c", script], env=env,
                                        capture_output=True, text=True, check=True)
                root = Path(value) if value and value.startswith("/") else self.test_home / ".local/share"
                self.assertEqual(result.stdout.splitlines(), [str(root / suffix) for suffix in
                                 ("fluorine/bin", "applications", "icons/hicolor/256x256/apps")])

    def test_packaging_keeps_host_runtimes_out_of_bundle(self):
        source = (Path(__file__).resolve().parents[2] / "docker/build-inner.sh").read_text()
        block = source.split("# Libraries that MUST come from the host", 1)[1]
        block = block[block.index("SKIP_PATTERN="):].split('echo "Dependencies bundled."', 1)[0]
        host_libs = ("libnss3.so", "libnssutil3.so", "libsmime3.so", "libssl3.so",
                     "libnspr4.so", "libplc4.so", "libplds4.so", "libsoftokn3.so",
                     "libfreebl3.so", "libfreeblpriv3.so", "libnssckbi.so",
                     "libnssdbm3.so", "libnss_files.so.2", "libfontconfig.so.1")
        deps = self.root / "deps"
        deps.mkdir()
        lines = []
        for name in (*host_libs, "libz.so.1"):
            (deps / name).touch()
            lines.append(f"{name} => {deps / name} (0x0)")
        tool_dir = self.root / "tools"
        tool_dir.mkdir()
        self.write_executable(tool_dir / "ldd", "#!/bin/sh\ncat <<'DEPS'\n" + "\n".join(lines) + "\nDEPS\n")
        (self.bundle / "plugins").mkdir()
        env = self.env.copy()
        env["PATH"] = str(tool_dir) + os.pathsep + env["PATH"]
        script = "set -euo pipefail\nOUT_DIR=" + shlex.quote(str(self.bundle)) + "\n" + block
        subprocess.run(["bash", "-c", script], env=env, check=True, capture_output=True)
        self.assertTrue((self.bundle / "lib/libz.so.1").exists())
        for name in host_libs:
            self.assertFalse((self.bundle / "lib" / name).exists(), name)

    def test_installer_extracts_into_custom_data_home(self):
        source = (Path(__file__).resolve().parents[2] / "docker/build-inner.sh").read_text()
        header = source.split("<<'INSTALLER_HEADER'\n", 1)[1].split("\nINSTALLER_HEADER", 1)[0]
        icons = self.bundle / "icons"
        icons.mkdir()
        (icons / "com.fluorine.manager.png").write_bytes(b"test icon")
        payload = self.root / "payload.tar.gz"
        with tarfile.open(payload, "w:gz") as archive:
            archive.add(self.bundle, arcname="fluorine-manager")
        installer = self.root / "installer.bin"
        installer.write_bytes((header + "\n").encode() + payload.read_bytes())
        data = self.root / "custom data"
        env = self.env.copy()
        env.update(HOME=str(self.test_home), XDG_DATA_HOME=str(data))
        subprocess.run(["bash", str(installer)], input="1\nn\n", env=env,
                       capture_output=True, text=True, check=True, timeout=15)
        installed = data / "fluorine/bin"
        self.assertTrue((installed / "ModOrganizer-core").exists())
        desktop = data / "applications/com.fluorine.manager.desktop"
        self.assertIn(f'Exec="{installed}/fluorine-manager" %u', desktop.read_text())
        self.assertTrue((data / "icons/hicolor/256x256/apps/com.fluorine.manager.png").exists())
        self.assertFalse((self.test_home / ".local/share").exists())

    # ── stale-file pruning ──
    REPO = Path(__file__).resolve().parents[2]
    PRUNE = REPO / "docker/fluorine-prune"
    # A real new list always names the launcher and the prune script itself.
    VALID_NEW = "fluorine-manager\nlibexec/fluorine-prune\n"

    def add_prune_support(self):
        libexec = self.bundle / "libexec"
        libexec.mkdir(exist_ok=True)
        shutil.copy(self.PRUNE, libexec / "fluorine-prune")
        (libexec / "fluorine-prune").chmod(0o755)
        with (self.bundle / "fluorine-manifest.txt").open("a") as manifest:
            manifest.write("libexec\nplugins\nfluorine-files.txt\n")
        (self.bundle / "plugins").mkdir(exist_ok=True)

    def put(self, root, rel, text="x"):
        path = root / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)

    def write_files_list(self, root, extra=()):
        entries = {str(p.relative_to(root)) for p in root.rglob("*")
                   if (p.is_file() or p.is_symlink())} - {"fluorine-files.txt"}
        entries.update(extra)
        (root / "fluorine-files.txt").write_text(
            "".join(f"{e}\n" for e in sorted(entries)))

    def seed_shipped_v1(self):
        """Bundle v1 ships files that v2 drops; returns after installing v1."""
        self.add_prune_support()
        for rel in ("plugins/games/old.py", "plugins/games/keep.py",
                    "plugins/emptied/old.py", "plugins/held/old.py"):
            self.put(self.bundle, rel)
        for rel in ("plugins/games", "plugins/emptied", "plugins/held"):
            pyc = self.bundle / rel / "__pycache__/old.cpython-312.pyc"
            if (self.bundle / rel / "old.py").exists():
                self.put(self.bundle, str(pyc.relative_to(self.bundle)))
        self.write_files_list(self.bundle)
        self.launch(self.bundle / "fluorine-manager")
        self.put(self.installed, "plugins/games/user.py", "mine")
        self.put(self.installed, "plugins/held/user.py", "mine")
        # Imported by the app after install, so it is not in either list.
        self.put(self.installed, "plugins/games/__pycache__/keep.cpython-312.pyc")

    def drop_from_bundle_v2(self):
        for rel in ("plugins/games/old.py", "plugins/emptied/old.py",
                    "plugins/held/old.py"):
            (self.bundle / rel).unlink()
        shutil.rmtree(self.bundle / "plugins/emptied")
        for rel in ("plugins/games", "plugins/held"):
            shutil.rmtree(self.bundle / rel / "__pycache__", ignore_errors=True)
        self.write_files_list(self.bundle)
        (self.bundle / "fluorine-bundle-version.txt").write_text("test-bundle-2")

    def assert_pruned(self, root):
        self.assertFalse((root / "plugins/games/old.py").exists())
        self.assertFalse((root / "plugins/games/__pycache__/old.cpython-312.pyc").exists())
        self.assertTrue((root / "plugins/games/keep.py").exists())
        self.assertEqual((root / "plugins/games/user.py").read_text(), "mine")
        self.assertTrue((root / "plugins/games/__pycache__/keep.cpython-312.pyc").exists())
        self.assertFalse((root / "plugins/emptied").exists())
        self.assertFalse((root / "plugins/held/old.py").exists())
        self.assertEqual((root / "plugins/held/user.py").read_text(), "mine")
        self.assertTrue(root.is_dir())

    def test_launcher_upgrade_prunes_files_dropped_from_bundle(self):
        self.seed_shipped_v1()
        self.drop_from_bundle_v2()
        self.launch(self.bundle / "fluorine-manager")
        self.assert_pruned(self.installed)
        self.assertEqual((self.installed / "fluorine-files.txt").read_text(),
                         (self.bundle / "fluorine-files.txt").read_text())

    def test_launcher_upgrade_without_old_list_prunes_nothing(self):
        self.add_prune_support()
        self.put(self.installed, "plugins/games/old.py")
        self.write_files_list(self.bundle)
        self.launch(self.bundle / "fluorine-manager")
        self.assertTrue((self.installed / "plugins/games/old.py").exists())
        self.assertTrue((self.installed / "fluorine-files.txt").exists())

    def test_launcher_ignores_unsafe_list_entries(self):
        self.add_prune_support()
        outside = self.installed.parent / "outside.txt"
        outside.parent.mkdir(parents=True)
        outside.write_text("keep")
        self.write_files_list(self.bundle)
        self.installed.mkdir(parents=True)
        # DEST is prepended to every entry, so an absolute entry only fails to
        # delete if the script never uses it as given. Point it at a real file.
        absolute = self.root / "absolute-target.txt"
        absolute.write_text("keep")
        (self.installed / "fluorine-files.txt").write_text(
            f"../outside.txt\n{outside}\n{absolute}\n\n"
            "plugins/../../outside.txt\n")
        self.launch(self.bundle / "fluorine-manager")
        self.assertEqual(outside.read_text(), "keep")
        self.assertEqual(absolute.read_text(), "keep")

    def test_launcher_survives_crlf_list(self):
        self.add_prune_support()
        self.put(self.bundle, "plugins/keep.py")
        self.write_files_list(self.bundle)
        self.put(self.installed, "plugins/keep.py")
        self.put(self.installed, "plugins/user.py", "mine")
        (self.installed / "fluorine-files.txt").write_text(
            "plugins/keep.py\r\nplugins/user.py\r\n")
        self.launch(self.bundle / "fluorine-manager")
        self.assertTrue((self.installed / "plugins/keep.py").exists())
        self.assertEqual((self.installed / "plugins/user.py").read_text(), "mine")

    def test_launcher_prunes_even_if_prune_script_is_not_executable(self):
        self.seed_shipped_v1()
        self.drop_from_bundle_v2()
        (self.bundle / "libexec/fluorine-prune").chmod(0o644)
        self.launch(self.bundle / "fluorine-manager")
        self.assert_pruned(self.installed)

    def build_installer(self):
        header = self.REPO.joinpath("docker/build-inner.sh").read_text()
        header = header.split("<<'INSTALLER_HEADER'\n", 1)[1].split("\nINSTALLER_HEADER", 1)[0]
        payload = self.root / "payload.tar.gz"
        with tarfile.open(payload, "w:gz") as archive:
            archive.add(self.bundle, arcname="fluorine-manager")
        installer = self.root / "installer.bin"
        installer.write_bytes((header + "\n").encode() + payload.read_bytes())
        return installer

    def run_installer(self, installer, data):
        env = self.env.copy()
        env.update(HOME=str(self.test_home), XDG_DATA_HOME=str(data))
        return subprocess.run(["bash", str(installer)], input="1\nn\n", env=env,
                              capture_output=True, text=True, check=True, timeout=15)

    def test_installer_upgrade_prunes_files_dropped_from_bundle(self):
        data = self.root / "data"
        self.env["XDG_DATA_HOME"] = str(data)
        self.installed = data / "fluorine/bin"
        icons = self.bundle / "icons"
        icons.mkdir()
        (icons / "com.fluorine.manager.png").write_bytes(b"icon")
        tmpdir = self.root / "tmp"
        tmpdir.mkdir()
        self.env["TMPDIR"] = str(tmpdir)
        self.add_prune_support()
        for rel in ("plugins/games/old.py", "plugins/games/keep.py",
                    "plugins/emptied/old.py", "plugins/held/old.py",
                    "plugins/games/__pycache__/old.cpython-312.pyc"):
            self.put(self.bundle, rel)
        self.write_files_list(self.bundle)
        self.run_installer(self.build_installer(), data)
        self.assertTrue((self.installed / "plugins/emptied/old.py").exists())
        self.put(self.installed, "plugins/games/user.py", "mine")
        self.put(self.installed, "plugins/held/user.py", "mine")
        self.put(self.installed, "plugins/games/__pycache__/keep.cpython-312.pyc")

        self.drop_from_bundle_v2()
        self.run_installer(self.build_installer(), data)
        self.assert_pruned(self.installed)
        self.assertEqual(list(tmpdir.iterdir()), [])

    def test_prune_script_skips_directories_and_reports_nothing_for_missing_list(self):
        dest = self.root / "dest"
        (dest / "plugins/dir").mkdir(parents=True)
        (dest / "plugins/dir/user.txt").write_text("mine")
        old, new = self.root / "old.txt", self.root / "new.txt"
        old.write_text("plugins/dir\n")
        new.write_text(self.VALID_NEW)
        run = lambda o: subprocess.run([str(self.PRUNE), str(o), str(new), str(dest)],
                                       capture_output=True, text=True)
        self.assertEqual(run(old).returncode, 0)
        self.assertEqual((dest / "plugins/dir/user.txt").read_text(), "mine")
        missing = run(self.root / "nope.txt")
        self.assertEqual((missing.returncode, missing.stdout, missing.stderr), (0, "", ""))

    def test_prune_script_does_not_follow_symlinked_directories(self):
        dest = self.root / "dest"
        elsewhere = self.root / "elsewhere"
        elsewhere.mkdir()
        (elsewhere / "old.py").write_text("user data")
        dest.mkdir()
        (dest / "linked").symlink_to(elsewhere)
        old, new = self.root / "old.txt", self.root / "new.txt"
        old.write_text("linked/old.py\n")
        new.write_text(self.VALID_NEW)
        subprocess.run([str(self.PRUNE), str(old), str(new), str(dest)], capture_output=True)
        self.assertTrue((elsewhere / "old.py").exists())

    def run_prune(self, old_text, new_text, dest, cwd=None):
        old, new = self.root / "old.txt", self.root / "new.txt"
        old.write_text(old_text)
        new.write_text(new_text)
        return subprocess.run([str(self.PRUNE), str(old), str(new), str(dest)],
                              capture_output=True, text=True, cwd=cwd)

    def test_prune_script_refuses_empty_or_truncated_new_list(self):
        for new_text in ("", "fluorine-manager\n", "libexec/fluorine-prune\n",
                         "plugins/a.py\n"):
            with self.subTest(new_text=new_text):
                dest = self.root / "dest"
                shutil.rmtree(dest, ignore_errors=True)
                self.put(dest, "fluorine-manager")
                self.put(dest, "plugins/a.py")
                result = self.run_prune(
                    "fluorine-manager\nlibexec/fluorine-prune\nplugins/a.py\n",
                    new_text, dest)
                self.assertEqual(result.returncode, 1)
                self.assertNotEqual(result.stderr, "")
                self.assertTrue((dest / "fluorine-manager").exists())
                self.assertTrue((dest / "plugins/a.py").exists())

    def test_prune_script_matches_symlinked_parent_with_glob_characters(self):
        dest = self.root / "dest"
        elsewhere = self.root / "elsewhere"
        self.put(elsewhere, "old.py", "user data")
        dest.mkdir()
        (dest / "[ab]").symlink_to(elsewhere)
        cwd = self.root / "cwd"
        (cwd / "a").mkdir(parents=True)
        (cwd / "b").mkdir()
        self.run_prune("[ab]/old.py\n", self.VALID_NEW, dest, cwd=cwd)
        self.assertTrue((elsewhere / "old.py").exists())

    def test_prune_script_does_not_follow_symlinked_pycache(self):
        dest = self.root / "dest"
        elsewhere = self.root / "elsewhere"
        self.put(elsewhere, "old.cpython-312.pyc", "user data")
        self.put(dest, "pkg/old.py")
        (dest / "pkg/__pycache__").symlink_to(elsewhere)
        self.run_prune("pkg/old.py\n", self.VALID_NEW, dest)
        self.assertFalse((dest / "pkg/old.py").exists())
        self.assertTrue((elsewhere / "old.cpython-312.pyc").exists())

    def test_prune_script_removes_only_the_modules_own_pycs(self):
        dest = self.root / "dest"
        cache = "pkg/__pycache__/"
        for name in ("old.cpython-312.pyc", "old.cpython-312.opt-1.pyc",
                     "old.sub.cpython-312.pyc", "older.cpython-312.pyc"):
            self.put(dest, cache + name)
        self.put(dest, "pkg/old.py")
        self.run_prune("pkg/old.py\n", self.VALID_NEW, dest)
        self.assertFalse((dest / cache / "old.cpython-312.pyc").exists())
        self.assertFalse((dest / cache / "old.cpython-312.opt-1.pyc").exists())
        self.assertTrue((dest / cache / "old.sub.cpython-312.pyc").exists())
        self.assertTrue((dest / cache / "older.cpython-312.pyc").exists())

    def test_packaging_writes_sorted_file_list_and_ships_prune_script(self):
        source = self.REPO.joinpath("docker/build-inner.sh").read_text()
        block = source.split("# ── Content-derived bundle identity ──", 1)[1]
        block = block.split("# ── Determine build mode ──", 1)[0]
        self.add_prune_support()
        self.put(self.bundle, "plugins/B.py")
        self.put(self.bundle, "plugins/a.py")
        (self.bundle / "plugins/link").symlink_to("a.py")
        for name in ("fluorine-manifest.txt", "fluorine-files.txt"):
            (self.bundle / name).unlink(missing_ok=True)
        script = "set -euo pipefail\nOUT_DIR=" + shlex.quote(str(self.bundle)) + "\n" + block
        subprocess.run(["bash", "-c", script], env=self.env, check=True, capture_output=True)
        lines = (self.bundle / "fluorine-files.txt").read_text().splitlines()
        self.assertEqual(lines, sorted(lines, key=lambda l: l.encode()))
        self.assertNotIn("fluorine-files.txt", lines)
        for want in ("plugins/B.py", "plugins/link", "fluorine-manager",
                     "fluorine-manifest.txt", "fluorine-bundle-version.txt",
                     "libexec/fluorine-prune"):
            self.assertIn(want, lines)
        self.assertFalse(any(l.startswith("./") or not l for l in lines))
        self.assertIn("fluorine-files.txt",
                      (self.bundle / "fluorine-manifest.txt").read_text().splitlines())


if __name__ == "__main__":
    unittest.main()

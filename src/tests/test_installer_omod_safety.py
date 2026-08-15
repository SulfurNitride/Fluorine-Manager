import importlib.util
import io
import pathlib
import struct
import sys
import tempfile
import types
import unittest
import zlib


ROOT = pathlib.Path(__file__).resolve().parents[2]
PLUGIN = ROOT / "src" / "plugins" / "installer_omod.py"


def load_plugin():
    mobase = types.ModuleType("mobase")
    mobase.IPluginInstallerCustom = type("IPluginInstallerCustom", (), {})
    mobase.IOrganizer = type("IOrganizer", (), {})
    mobase.IPlugin = type("IPlugin", (), {})
    mobase.GuessedString = type("GuessedString", (), {})
    mobase.VersionInfo = lambda *args: args
    mobase.PluginSetting = object
    mobase.InstallResult = types.SimpleNamespace(
        FAILED=0, NOT_ATTEMPTED=1, SUCCESS=2
    )
    sys.modules["mobase"] = mobase

    spec = importlib.util.spec_from_file_location("installer_omod_under_test", PLUGIN)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


OMOD = load_plugin()


def net_string(value: str) -> bytes:
    raw = value.encode("utf-8")
    if len(raw) >= 0x80:
        raise ValueError("test helper supports only short strings")
    return bytes([len(raw)]) + raw


class OmodPathSafetyTests(unittest.TestCase):
    def test_safe_nested_member(self):
        with tempfile.TemporaryDirectory() as temporary:
            expected = pathlib.Path(temporary) / "meshes" / "actors" / "a.nif"
            actual = OMOD._safe_member_path(temporary, r"meshes\actors/a.nif")
            self.assertEqual(expected, actual)

    def test_rejects_escape_spellings(self):
        with tempfile.TemporaryDirectory() as temporary:
            for member in (
                "",
                "/absolute",
                r"\server\share",
                r"C:\outside",
                "../outside",
                r"safe\..\outside",
                "safe/./file",
                "safe//file",
                "safe/",
                "nul\x00file",
            ):
                with self.subTest(member=member):
                    with self.assertRaises(ValueError):
                        OMOD._safe_member_path(temporary, member)

    def test_rejects_symlinked_parent(self):
        with tempfile.TemporaryDirectory() as temporary, tempfile.TemporaryDirectory() as outside:
            (pathlib.Path(temporary) / "linked").symlink_to(
                outside, target_is_directory=True
            )
            with self.assertRaises(ValueError):
                OMOD._safe_member_path(temporary, "linked/file")
            self.assertEqual([], list(pathlib.Path(outside).iterdir()))

    def test_rejects_symlinked_root(self):
        with tempfile.TemporaryDirectory() as parent, tempfile.TemporaryDirectory() as real:
            root = pathlib.Path(parent) / "root"
            root.symlink_to(real, target_is_directory=True)
            with self.assertRaises(ValueError):
                OMOD._safe_member_path(root, "file")

    def test_crc_rejects_negative_sizes(self):
        installer = OMOD.OmodInstaller()
        crc = bytes([1]) + net_string("file") + struct.pack("<I", 0) + struct.pack("<q", -1)
        with self.assertRaises(ValueError):
            installer._parse_crc_file(crc)

    def test_net_string_rejects_truncation_and_invalid_utf8(self):
        with self.assertRaises(EOFError):
            OMOD.OmodInstaller._read_net_string(io.BytesIO(b"\x04ab"))
        with self.assertRaises(UnicodeDecodeError):
            OMOD.OmodInstaller._read_net_string(io.BytesIO(b"\x01\xff"))
        with self.assertRaises(EOFError):
            OMOD.OmodInstaller._read_7bit_encoded_int(io.BytesIO(b"\x80"))

    def test_stream_escape_fails_without_outside_write(self):
        installer = OMOD.OmodInstaller()
        compressed = zlib.compressobj(wbits=-15)
        payload = compressed.compress(b"owned") + compressed.flush()

        class FakeZip:
            def read(self, name):
                if name == "data.crc":
                    return (
                        bytes([1])
                        + net_string("../outside")
                        + struct.pack("<I", 0)
                        + struct.pack("<q", 5)
                    )
                return payload

        with tempfile.TemporaryDirectory() as parent:
            root = pathlib.Path(parent) / "root"
            root.mkdir()
            outside = pathlib.Path(parent) / "outside"
            with self.assertRaises(ValueError):
                installer._extract_stream(
                    FakeZip(), ["data", "data.crc"], "data", "data.crc", 0, str(root)
                )
            self.assertFalse(outside.exists())

    def test_truncated_stream_does_not_succeed(self):
        installer = OMOD.OmodInstaller()
        compressed = zlib.compressobj(wbits=-15)
        payload = compressed.compress(b"x") + compressed.flush()

        class FakeZip:
            def read(self, name):
                if name == "data.crc":
                    return (
                        bytes([1])
                        + net_string("file")
                        + struct.pack("<I", 0)
                        + struct.pack("<q", 2)
                    )
                return payload

        with tempfile.TemporaryDirectory() as root:
            with self.assertRaises(ValueError):
                installer._extract_stream(
                    FakeZip(), ["data", "data.crc"], "data", "data.crc", 0, root
                )
            self.assertFalse((pathlib.Path(root) / "file").exists())


if __name__ == "__main__":
    unittest.main()

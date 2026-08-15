# -*- encoding: utf-8 -*-

"""
OMOD Installer plugin for Mod Organizer 2 (Linux port).

Handles .omod archives (Oblivion Mod Manager format) by parsing the binary
config, extracting compressed data/plugin streams, and re-packaging as a
standard zip for MO2's installation manager.
"""

import io
import lzma
import os
import shutil
import struct
import sys
import tempfile
import zipfile
import zlib
from pathlib import Path

import mobase


def _log_error(msg: str) -> None:
    print(f"[OMOD] {msg}", file=sys.stderr)


def _log_warn(msg: str) -> None:
    print(f"[OMOD] WARNING: {msg}", file=sys.stderr)


def _safe_member_path(root: str | Path, member: str) -> Path:
    """Resolve an OMOD-controlled member beneath a private extraction root."""
    if not member or "\x00" in member:
        raise ValueError("empty or NUL-containing OMOD member path")

    normalized = member.replace("\\", "/")
    if normalized.startswith("/"):
        raise ValueError(f"absolute OMOD member path: {member!r}")
    if len(normalized) >= 2 and normalized[0].isalpha() and normalized[1] == ":":
        raise ValueError(f"drive-qualified OMOD member path: {member!r}")

    components = normalized.split("/")
    if any(component in ("", ".", "..") for component in components):
        raise ValueError(f"unsafe OMOD member path: {member!r}")

    configured_root = Path(root)
    if configured_root.is_symlink():
        raise ValueError("OMOD extraction root is a symbolic link")
    root_path = configured_root.resolve(strict=True)
    if not root_path.is_dir():
        raise ValueError("OMOD extraction root is not a real directory")

    destination = root_path.joinpath(*components)
    destination.relative_to(root_path)

    current = root_path
    for component in components[:-1]:
        current /= component
        if current.is_symlink():
            raise ValueError(f"OMOD member traverses a symbolic link: {member!r}")
        if current.exists() and not current.is_dir():
            raise ValueError(f"OMOD member parent is not a directory: {member!r}")

    if destination.is_symlink():
        raise ValueError(f"OMOD member collides with a symbolic link: {member!r}")
    return destination


class OmodInstaller(mobase.IPluginInstallerCustom):
    _organizer: mobase.IOrganizer

    def init(self, organizer: mobase.IOrganizer) -> bool:
        self._organizer = organizer
        return True

    def name(self) -> str:
        return "OMOD Installer"

    def localizedName(self) -> str:
        return "OMOD Installer"

    def author(self) -> str:
        return "Fluorine Manager"

    def description(self) -> str:
        return "Installer for .omod archives (Oblivion Mod Manager format)"

    def version(self) -> mobase.VersionInfo:
        return mobase.VersionInfo(1, 0, 0)

    def settings(self) -> list[mobase.PluginSetting]:
        return []

    def priority(self) -> int:
        return 500

    def isManualInstaller(self) -> bool:
        return False

    def isArchiveSupported(self, archive) -> bool:
        # Called with IFileTree (mod list refresh) or str (install check).
        if isinstance(archive, str):
            return archive.lower().endswith(".omod")
        # IFileTree: look for the "config" file that every OMOD contains.
        try:
            for entry in archive:
                if entry.isFile() and entry.name() == "config":
                    return True
        except TypeError:
            pass
        return False

    def supportedExtensions(self) -> set[str]:
        return {"omod"}

    def install(
        self,
        mod_name: mobase.GuessedString,
        game_name: str,
        archive_name: str,
        version: str,
        nexus_id: int,
    ) -> mobase.InstallResult:
        try:
            return self._do_install(mod_name, archive_name)
        except Exception as e:
            _log_error(f"OMOD install failed: {e}")
            return mobase.InstallResult.FAILED

    def _do_install(
        self,
        mod_name: mobase.GuessedString,
        archive_name: str,
    ) -> mobase.InstallResult:
        with zipfile.ZipFile(archive_name, "r") as zf:
            names = zf.namelist()

            if "config" not in names:
                _log_warn("no config entry found")
                return mobase.InstallResult.NOT_ATTEMPTED

            config = self._parse_config(zf.read("config"))
            if config.get("mod_name"):
                mod_name.update(config["mod_name"])

            compression = config.get("compression_type", 0)

            tmpdir = tempfile.mkdtemp(prefix="omod_")
            try:
                self._extract_stream(
                    zf, names, "data", "data.crc", compression, tmpdir
                )
                self._extract_stream(
                    zf, names, "plugins", "plugins.crc", compression, tmpdir
                )

                # If the OMOD contains a readme, extract it too.
                for entry in names:
                    lower = entry.lower()
                    if lower == "readme" or lower.startswith("readme."):
                        data = zf.read(entry)
                        dest = _safe_member_path(tmpdir, entry)
                        dest.parent.mkdir(parents=True, exist_ok=True)
                        dest.write_bytes(data)

                # Collect extracted files.
                file_list = []
                for root, _dirs, files in os.walk(tmpdir):
                    for f in files:
                        full = os.path.join(root, f)
                        rel = os.path.relpath(full, tmpdir)
                        file_list.append(rel)

                if not file_list:
                    _log_warn("no files extracted")
                    return mobase.InstallResult.FAILED

                # Repackage as a standard zip for MO2's installer.
                repack_path = os.path.join(tmpdir, "_repack.zip")
                with zipfile.ZipFile(repack_path, "w", zipfile.ZIP_DEFLATED) as out_zip:
                    for rel in file_list:
                        out_zip.write(os.path.join(tmpdir, rel), rel)

                result, _, _ = self.manager().installArchive(mod_name, repack_path)
                return result
            finally:
                shutil.rmtree(tmpdir, ignore_errors=True)

    def _extract_stream(
        self,
        zf: zipfile.ZipFile,
        names: list[str],
        stream_name: str,
        crc_name: str,
        compression: int,
        out_dir: str,
    ) -> None:
        if stream_name not in names:
            return
        if crc_name not in names:
            _log_warn(f"{stream_name} present but {crc_name} missing")
            return

        file_list = self._parse_crc_file(zf.read(crc_name))
        if not file_list:
            return

        raw = zf.read(stream_name)
        decompressed = self._decompress_stream(raw, compression)

        offset = 0
        for path, size in file_list:
            if offset + size > len(decompressed):
                raise ValueError(
                    f"truncated stream for {path} "
                    f"(need {size} bytes at offset {offset}, "
                    f"have {len(decompressed)})"
                )
                break

            file_data = decompressed[offset : offset + size]
            offset += size

            dest = _safe_member_path(out_dir, path)
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(file_data)

    def _parse_config(self, data: bytes) -> dict:
        """Parse the OMOD binary config entry."""
        reader = io.BytesIO(data)
        config = {}

        config["file_version"] = struct.unpack("<B", reader.read(1))[0]
        config["mod_name"] = self._read_net_string(reader)

        # Major and minor version.
        config["major"] = struct.unpack("<i", reader.read(4))[0]
        config["minor"] = struct.unpack("<i", reader.read(4))[0]

        config["author"] = self._read_net_string(reader)
        config["email"] = self._read_net_string(reader)
        config["website"] = self._read_net_string(reader)
        config["description"] = self._read_net_string(reader)

        # Creation time (Windows FILETIME, 8 bytes) - skip.
        reader.read(8)

        # Compression type: 0 = deflate, 1 = lzma.
        config["compression_type"] = struct.unpack("<B", reader.read(1))[0]

        return config

    def _parse_crc_file(self, data: bytes) -> list[tuple[str, int]]:
        """Parse data.crc or plugins.crc.

        Returns [(relative_path, uncompressed_size), ...].
        """
        reader = io.BytesIO(data)
        count = self._read_7bit_encoded_int(reader)
        files = []
        for _ in range(count):
            path = self._read_net_string(reader)
            _crc = struct.unpack("<I", reader.read(4))[0]
            size = struct.unpack("<q", reader.read(8))[0]
            if size < 0:
                raise ValueError(f"negative uncompressed size for {path!r}")
            files.append((path, size))
        return files

    def _decompress_stream(self, data: bytes, compression_type: int) -> bytes:
        """Decompress an OMOD data or plugins stream."""
        if compression_type == 0:
            # Raw deflate (no zlib/gzip header).
            return zlib.decompress(data, -15)
        elif compression_type == 1:
            return lzma.decompress(data)
        else:
            raise ValueError(f"Unknown OMOD compression type: {compression_type}")

    @staticmethod
    def _read_net_string(reader: io.BytesIO) -> str:
        """Read a .NET BinaryWriter-style length-prefixed string.

        The length is encoded as a 7-bit encoded int, followed by that many
        bytes of UTF-8.
        """
        length = OmodInstaller._read_7bit_encoded_int(reader)
        if length == 0:
            return ""
        raw = reader.read(length)
        if len(raw) != length:
            raise EOFError("truncated .NET string in OMOD metadata")
        return raw.decode("utf-8")

    @staticmethod
    def _read_7bit_encoded_int(reader: io.BytesIO) -> int:
        """Read a .NET 7-bit encoded integer."""
        result = 0
        shift = 0
        while True:
            byte_data = reader.read(1)
            if not byte_data:
                raise EOFError("truncated 7-bit integer in OMOD metadata")
            b = byte_data[0]
            result |= (b & 0x7F) << shift
            shift += 7
            if (b & 0x80) == 0:
                if result > 0x7FFFFFFF:
                    raise ValueError("7-bit integer exceeds signed .NET range")
                return result
            if shift >= 35:
                raise ValueError("invalid 7-bit integer in OMOD metadata")


def createPlugin() -> mobase.IPlugin:
    return OmodInstaller()

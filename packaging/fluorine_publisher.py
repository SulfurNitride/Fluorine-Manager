#!/usr/bin/env python3
"""Build and publish Fluorine bundles without overlaying a live installation."""

from __future__ import annotations

import argparse
import fcntl
import hashlib
import json
import os
import secrets
import shutil
import signal
import stat
import sys
import tempfile
import time
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import Any


MANIFEST_NAME = "fluorine-manifest-v2.json"
LEGACY_MANIFEST_NAME = "fluorine-manifest.txt"
VERSION_NAME = "fluorine-bundle-version.txt"
MARKER_NAME = ".version"
RECOVERY_NAME = "publish-recovery"
FORMAT = 2
LEGACY_TOMBSTONES = (
    "etc/fonts/fonts.conf",
    "lib/libfontconfig.so*",
    "lib/libssl.so*",
    "lib/libcrypto.so*",
)


class PublishError(RuntimeError):
    pass


def _sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _fsync_directory(path: Path) -> None:
    descriptor = os.open(path, os.O_RDONLY | os.O_DIRECTORY)
    try:
        os.fsync(descriptor)
    finally:
        os.close(descriptor)


def _durable_mkdir(path: Path, mode: int = 0o755) -> None:
    """Create a directory chain and persist every new directory entry."""
    missing: list[Path] = []
    current = path
    while True:
        try:
            info = current.lstat()
            if not stat.S_ISDIR(info.st_mode) or stat.S_ISLNK(info.st_mode):
                raise PublishError(f"unsafe publication directory: {current}")
            break
        except FileNotFoundError:
            missing.append(current)
            if current.parent == current:
                raise PublishError(f"cannot resolve directory chain for {path}")
            current = current.parent
    for directory in reversed(missing):
        os.mkdir(directory, mode)
        _fsync_directory(directory.parent)
        _fsync_directory(directory)


def _atomic_write(path: Path, data: bytes, mode: int = 0o644) -> None:
    _durable_mkdir(path.parent)
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{path.name}.fluorine-", dir=path.parent
    )
    temporary = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.chmod(temporary, mode)
        os.replace(temporary, path)
        _fsync_directory(path.parent)
    finally:
        temporary.unlink(missing_ok=True)


def _safe_relative(raw: str) -> PurePosixPath:
    if not raw or any(character in raw for character in "\x00\r\n\t"):
        raise PublishError(f"unsafe empty or control-character path: {raw!r}")
    path = PurePosixPath(raw)
    if path.is_absolute() or any(part in ("", ".", "..") for part in path.parts):
        raise PublishError(f"unsafe bundle path: {raw!r}")
    if str(path) != raw:
        raise PublishError(f"non-canonical bundle path: {raw!r}")
    if raw in (MANIFEST_NAME, MARKER_NAME):
        raise PublishError(f"reserved bundle path: {raw!r}")
    return path


def _relative_path(root: Path, path: Path) -> str:
    return path.relative_to(root).as_posix()


def _assert_directory_chain(root: Path, relative: PurePosixPath, create: bool) -> None:
    current = root
    for part in relative.parts[:-1]:
        current /= part
        try:
            info = current.lstat()
        except FileNotFoundError:
            if not create:
                raise PublishError(f"missing bundle parent directory: {current}")
            _durable_mkdir(current)
            continue
        if not stat.S_ISDIR(info.st_mode) or stat.S_ISLNK(info.st_mode):
            raise PublishError(f"bundle path has a non-directory parent: {current}")


def _safe_symlink_target(root: Path, path: Path, target: str) -> None:
    if not target or any(character in target for character in "\x00\r\n\t"):
        raise PublishError(f"unsafe symlink target for {path}: {target!r}")
    target_path = Path(target)
    if target_path.is_absolute():
        raise PublishError(f"absolute symlink target for {path}: {target!r}")
    resolved_root = root.resolve(strict=True)
    resolved_target = (path.parent / target).resolve(strict=False)
    try:
        resolved_target.relative_to(resolved_root)
    except ValueError as error:
        raise PublishError(f"symlink escapes bundle: {path} -> {target}") from error


def _scan_leaves(root: Path, *, exclude: set[str]) -> list[dict[str, Any]]:
    root = root.resolve(strict=True)
    entries: list[dict[str, Any]] = []

    def visit(directory: Path) -> None:
        for child in sorted(os.scandir(directory), key=lambda item: item.name):
            path = Path(child.path)
            relative = _relative_path(root, path)
            if relative in exclude:
                continue
            info = child.stat(follow_symlinks=False)
            mode = stat.S_IMODE(info.st_mode)
            if stat.S_ISLNK(info.st_mode):
                target = os.readlink(path)
                _safe_relative(relative)
                _safe_symlink_target(root, path, target)
                entries.append(
                    {
                        "path": relative,
                        "type": "symlink",
                        "mode": mode,
                        "sha256": _sha256_bytes(os.fsencode(target)),
                        "target": target,
                    }
                )
            elif stat.S_ISDIR(info.st_mode):
                visit(path)
            elif stat.S_ISREG(info.st_mode):
                _safe_relative(relative)
                entries.append(
                    {
                        "path": relative,
                        "type": "file",
                        "mode": mode,
                        "sha256": _sha256_file(path),
                    }
                )
            else:
                raise PublishError(f"unsupported bundle object: {path}")

    visit(root)
    entries.sort(key=lambda entry: entry["path"])
    return entries


def _manifest_bytes(entries: list[dict[str, Any]]) -> bytes:
    return (
        json.dumps(
            {"format": FORMAT, "entries": entries},
            ensure_ascii=False,
            sort_keys=True,
            separators=(",", ":"),
        ).encode("utf-8")
        + b"\n"
    )


def build_manifest(root: Path) -> str:
    root = root.resolve(strict=True)
    for metadata in (MANIFEST_NAME, LEGACY_MANIFEST_NAME, VERSION_NAME):
        (root / metadata).unlink(missing_ok=True)

    payload_entries = _scan_leaves(
        root, exclude={MANIFEST_NAME, LEGACY_MANIFEST_NAME, VERSION_NAME}
    )
    payload_identity = _sha256_bytes(_manifest_bytes(payload_entries))
    _atomic_write(
        root / VERSION_NAME,
        f"format=2\npayload_sha256={payload_identity}\n".encode("ascii"),
    )

    top_level = sorted(
        child.name
        for child in root.iterdir()
        if child.name != LEGACY_MANIFEST_NAME
    )
    _atomic_write(
        root / LEGACY_MANIFEST_NAME,
        ("\n".join(top_level) + "\n").encode("utf-8"),
    )

    entries = _scan_leaves(root, exclude={MANIFEST_NAME})
    manifest = _manifest_bytes(entries)
    _atomic_write(root / MANIFEST_NAME, manifest)
    validate_bundle(root)
    return _sha256_bytes(manifest)


def _load_manifest(path: Path) -> tuple[list[dict[str, Any]], bytes]:
    try:
        info = path.lstat()
        if not stat.S_ISREG(info.st_mode) or stat.S_ISLNK(info.st_mode):
            raise PublishError(f"manifest is not a regular file: {path}")
        raw = path.read_bytes()
        document = json.loads(raw)
    except (OSError, json.JSONDecodeError) as error:
        raise PublishError(f"cannot read bundle manifest {path}: {error}") from error
    if not isinstance(document, dict) or set(document) != {"format", "entries"}:
        raise PublishError(f"invalid bundle manifest structure: {path}")
    if document["format"] != FORMAT or not isinstance(document["entries"], list):
        raise PublishError(f"unsupported bundle manifest format: {path}")

    seen: dict[str, str] = {}
    entries: list[dict[str, Any]] = []
    previous = ""
    for entry in document["entries"]:
        if not isinstance(entry, dict):
            raise PublishError("manifest entry is not an object")
        entry_type = entry.get("type")
        expected_keys = (
            {"path", "type", "mode", "sha256"}
            if entry_type == "file"
            else {"path", "type", "mode", "sha256", "target"}
        )
        if entry_type not in ("file", "symlink") or set(entry) != expected_keys:
            raise PublishError(f"invalid manifest entry: {entry!r}")
        relative = str(_safe_relative(entry["path"]))
        if relative <= previous or relative in seen:
            raise PublishError("manifest paths are duplicated or not sorted")
        previous = relative
        if not isinstance(entry["mode"], int) or not 0 <= entry["mode"] <= 0o7777:
            raise PublishError(f"invalid mode for {relative}")
        digest = entry["sha256"]
        if not isinstance(digest, str) or len(digest) != 64:
            raise PublishError(f"invalid digest for {relative}")
        try:
            int(digest, 16)
        except ValueError as error:
            raise PublishError(f"invalid digest for {relative}") from error
        if entry_type == "symlink" and not isinstance(entry["target"], str):
            raise PublishError(f"invalid symlink target for {relative}")
        for parent in PurePosixPath(relative).parents:
            parent_text = str(parent)
            if parent_text == ".":
                break
            if seen.get(parent_text) in ("file", "symlink"):
                raise PublishError(f"manifest leaf is used as a parent: {parent_text}")
        seen[relative] = entry_type
        entries.append(entry)
    return entries, raw


def _entry_matches(root: Path, entry: dict[str, Any]) -> bool:
    relative = _safe_relative(entry["path"])
    try:
        _assert_directory_chain(root, relative, create=False)
    except PublishError:
        return False
    path = root.joinpath(*relative.parts)
    try:
        info = path.lstat()
    except FileNotFoundError:
        return False
    if entry["type"] == "file":
        return (
            stat.S_ISREG(info.st_mode)
            and not stat.S_ISLNK(info.st_mode)
            and stat.S_IMODE(info.st_mode) == entry["mode"]
            and _sha256_file(path) == entry["sha256"]
        )
    if not stat.S_ISLNK(info.st_mode):
        return False
    target = os.readlink(path)
    _safe_symlink_target(root, path, target)
    return target == entry["target"] and _sha256_bytes(os.fsencode(target)) == entry[
        "sha256"
    ]


def validate_bundle(root: Path, *, exact_inventory: bool = True) -> str:
    root = root.resolve(strict=True)
    entries, raw = _load_manifest(root / MANIFEST_NAME)
    expected = {entry["path"] for entry in entries}
    for entry in entries:
        if not _entry_matches(root, entry):
            raise PublishError(f"bundle entry does not match its manifest: {entry['path']}")
    if exact_inventory:
        actual = {
            entry["path"] for entry in _scan_leaves(root, exclude={MANIFEST_NAME})
        }
        if actual != expected:
            missing = sorted(expected - actual)
            extra = sorted(actual - expected)
            raise PublishError(
                f"bundle inventory mismatch (missing={missing!r}, extra={extra!r})"
            )
    return _sha256_bytes(raw)


def _ensure_owned_directory(path: Path, mode: int = 0o700) -> None:
    try:
        info = path.lstat()
    except FileNotFoundError:
        _durable_mkdir(path, mode)
        info = path.lstat()
    if not stat.S_ISDIR(info.st_mode) or stat.S_ISLNK(info.st_mode):
        raise PublishError(f"unsafe publication directory: {path}")
    if info.st_uid != os.geteuid():
        raise PublishError(f"publication directory is not owned by this user: {path}")


def _copy_entry(
    source: Path,
    destination: Path,
    entry: dict[str, Any],
    *,
    enable_checkpoint: bool = True,
    transaction_id: str = "",
) -> None:
    relative = _safe_relative(entry["path"])
    _assert_directory_chain(destination, relative, create=True)
    source_path = source.joinpath(*relative.parts)
    destination_path = destination.joinpath(*relative.parts)
    token = f"{transaction_id[:16]}-" if transaction_id else ""
    temporary: Path | None = None
    try:
        if entry["type"] == "file":
            descriptor = -1
            for _ in range(32):
                temporary = destination_path.with_name(
                    f".{destination_path.name}.fluorine-{token}{secrets.token_hex(8)}"
                )
                try:
                    descriptor = os.open(
                        temporary,
                        os.O_WRONLY | os.O_CREAT | os.O_EXCL,
                        0o600,
                    )
                    break
                except FileExistsError:
                    temporary = None
            if temporary is None or descriptor < 0:
                raise PublishError(
                    f"cannot allocate a temporary path for {entry['path']}"
                )
            with source_path.open("rb") as source_stream, os.fdopen(
                descriptor, "wb"
            ) as destination_stream:
                shutil.copyfileobj(source_stream, destination_stream)
                destination_stream.flush()
                os.fsync(destination_stream.fileno())
            os.chmod(temporary, entry["mode"])
            with temporary.open("rb") as stream:
                os.fsync(stream.fileno())
        else:
            for _ in range(32):
                temporary = destination_path.with_name(
                    f".{destination_path.name}.fluorine-{token}{secrets.token_hex(8)}"
                )
                try:
                    os.symlink(entry["target"], temporary)
                    break
                except FileExistsError:
                    temporary = None
            if temporary is None:
                raise PublishError(
                    f"cannot allocate a temporary path for {entry['path']}"
                )
        temporary_entry = {
            **entry,
            "path": _relative_path(destination, temporary),
        }
        if not _entry_matches(destination, temporary_entry):
            raise PublishError(f"copied entry failed verification: {entry['path']}")
        if enable_checkpoint:
            _checkpoint(f"before-replace:{entry['path']}")
        try:
            target_info = destination_path.lstat()
        except FileNotFoundError:
            target_info = None
        if target_info is not None and stat.S_ISDIR(target_info.st_mode):
            destination_path.rmdir()
        os.replace(temporary, destination_path)
        _fsync_directory(destination_path.parent)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def _stage_bundle(source: Path, data_root: Path) -> tuple[Path, str]:
    bundle_id = validate_bundle(source)
    stage_root = data_root / "publish-stage"
    _ensure_owned_directory(stage_root)
    final = stage_root / bundle_id
    if final.exists():
        if validate_bundle(final) != bundle_id:
            raise PublishError(f"existing publication stage is invalid: {final}")
        return final, bundle_id

    incoming = stage_root / f".incoming-{bundle_id}-{os.getpid()}"
    if incoming.exists():
        shutil.rmtree(incoming)
    _durable_mkdir(incoming, 0o700)
    entries, raw = _load_manifest(source / MANIFEST_NAME)
    try:
        for entry in entries:
            _copy_entry(
                source,
                incoming,
                entry,
                enable_checkpoint=False,
                transaction_id=bundle_id,
            )
        _atomic_write(incoming / MANIFEST_NAME, raw)
        if validate_bundle(incoming) != bundle_id:
            raise PublishError("staged bundle identity changed")
        try:
            os.rename(incoming, final)
        except FileExistsError:
            if validate_bundle(final) != bundle_id:
                raise PublishError(f"competing publication stage is invalid: {final}")
            shutil.rmtree(incoming)
        _fsync_directory(stage_root)
    except Exception:
        shutil.rmtree(incoming, ignore_errors=True)
        raise
    return final, bundle_id


def _load_legacy_roots(path: Path) -> list[str]:
    roots: list[str] = []
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line:
            continue
        relative = _safe_relative(line)
        if len(relative.parts) != 1:
            raise PublishError(f"legacy manifest contains a nested path: {line!r}")
        roots.append(line)
    if len(roots) != len(set(roots)):
        raise PublishError("legacy manifest contains duplicate roots")
    return roots


def _transaction_metadata(path: Path) -> dict[str, Any]:
    try:
        document = json.loads((path / "transaction.json").read_bytes())
    except (OSError, json.JSONDecodeError) as error:
        raise PublishError(f"invalid publication transaction: {error}") from error
    if not isinstance(document, dict) or set(document) != {
        "bundle_id",
        "stage",
        "destination",
        "old_format",
        "quarantine",
    }:
        raise PublishError("invalid publication transaction fields")
    bundle_id = document["bundle_id"]
    if not isinstance(bundle_id, str) or len(bundle_id) != 64:
        raise PublishError("invalid publication transaction identity")
    try:
        int(bundle_id, 16)
    except ValueError as error:
        raise PublishError("invalid publication transaction identity") from error
    data_root = path.parent.resolve(strict=True)
    expected_stage = data_root / "publish-stage" / bundle_id
    expected_quarantine_parent = data_root / "legacy-quarantine"
    if Path(document["stage"]).absolute() != expected_stage.absolute():
        raise PublishError("publication transaction names an unexpected stage")
    if not isinstance(document["destination"], str):
        raise PublishError("publication transaction has an invalid destination")
    quarantine = Path(document["quarantine"]).absolute()
    try:
        quarantine.relative_to(expected_quarantine_parent.absolute())
    except ValueError as error:
        raise PublishError("publication transaction names an unsafe quarantine") from error
    if quarantine.parent != expected_quarantine_parent.absolute():
        raise PublishError("publication transaction quarantine is not a direct child")
    if document["old_format"] not in ("empty", "v1", "v2"):
        raise PublishError("publication transaction has an invalid old format")
    return document


def _inspect_installation(
    destination: Path, bootstrap_stage: Path | None = None
) -> tuple[str, list[dict[str, Any]], bytes | None, list[str]]:
    old_v2 = destination / MANIFEST_NAME
    old_v1 = destination / LEGACY_MANIFEST_NAME
    if old_v2.exists() or old_v2.is_symlink():
        entries, raw = _load_manifest(old_v2)
        identity = _sha256_bytes(raw)
        marker = destination / MARKER_NAME
        if (
            not marker.is_file()
            or marker.read_bytes() != f"manifest:{identity}\n".encode("ascii")
        ):
            raise PublishError(
                "installed manifest is not authenticated by its commit marker"
            )
        return "v2", entries, raw, []
    if old_v1.exists() or old_v1.is_symlink():
        return "v1", [], None, _load_legacy_roots(old_v1)
    existing = list(destination.iterdir())
    if bootstrap_stage is not None and len(existing) == 1:
        launcher_entries, _ = _load_manifest(bootstrap_stage / MANIFEST_NAME)
        launcher = next(
            (
                entry
                for entry in launcher_entries
                if entry["path"] == "fluorine-manager"
            ),
            None,
        )
        if launcher is not None and _entry_matches(destination, launcher):
            return "empty", [], None, []
    if existing:
        raise PublishError(
            "existing installation has no valid Fluorine manifest; refusing to guess ownership"
        )
    return "empty", [], None, []


def _create_transaction(
    data_root: Path,
    destination: Path,
    stage: Path,
    bundle_id: str,
    installation: tuple[str, list[dict[str, Any]], bytes | None, list[str]],
) -> Path:
    transaction = data_root / "publish-transaction"
    if transaction.exists():
        raise PublishError("a publication transaction is already active")
    temporary = data_root / f".publish-transaction-{os.getpid()}"
    if temporary.exists():
        shutil.rmtree(temporary)
    _durable_mkdir(temporary, 0o700)
    _durable_mkdir(temporary / "backup", 0o700)
    try:
        old_format, _, old_manifest_raw, roots = installation
        if old_manifest_raw is not None:
            _atomic_write(temporary / "old-v2.json", old_manifest_raw, 0o600)
        elif old_format == "v1":
            _atomic_write(
                temporary / "old-v1.txt",
                ("\n".join(roots) + "\n").encode("utf-8"),
                0o600,
            )

        quarantine = str(
            data_root
            / "legacy-quarantine"
            / f"{time.strftime('%Y%m%d-%H%M%S')}-{bundle_id[:12]}"
        )
        metadata = {
            "bundle_id": bundle_id,
            "stage": str(stage),
            "destination": str(destination.absolute()),
            "old_format": old_format,
            "quarantine": quarantine,
        }
        _atomic_write(
            temporary / "transaction.json",
            json.dumps(metadata, sort_keys=True).encode("utf-8") + b"\n",
            0o600,
        )
        os.rename(temporary, transaction)
    except Exception:
        shutil.rmtree(temporary, ignore_errors=True)
        raise
    _fsync_directory(data_root)
    _checkpoint("after-journal")
    return transaction


def _move_leaf(source: Path, destination: Path) -> None:
    _durable_mkdir(destination.parent, 0o700)
    if destination.exists() or destination.is_symlink():
        if not (source.exists() or source.is_symlink()):
            return
        raise PublishError(f"publication backup collision: {destination}")
    os.replace(source, destination)
    _fsync_directory(source.parent)
    _fsync_directory(destination.parent)


def _same_leaf(left: Path, right: Path) -> bool:
    try:
        left_info = left.lstat()
        right_info = right.lstat()
    except FileNotFoundError:
        return False
    if stat.S_ISREG(left_info.st_mode) and stat.S_ISREG(right_info.st_mode):
        return (
            stat.S_IMODE(left_info.st_mode) == stat.S_IMODE(right_info.st_mode)
            and _sha256_file(left) == _sha256_file(right)
        )
    if stat.S_ISLNK(left_info.st_mode) and stat.S_ISLNK(right_info.st_mode):
        return os.readlink(left) == os.readlink(right)
    return False


def _preserve_leaf(source: Path, destination: Path) -> None:
    """Persist a user-modified/colliding leaf without removing the source."""
    if destination.exists() or destination.is_symlink():
        if _same_leaf(source, destination):
            return
        raise PublishError(f"publication preservation collision: {destination}")
    _durable_mkdir(destination.parent, 0o700)
    info = source.lstat()
    if stat.S_ISREG(info.st_mode):
        descriptor, temporary_name = tempfile.mkstemp(
            prefix=f".{destination.name}.fluorine-", dir=destination.parent
        )
        temporary = Path(temporary_name)
        try:
            with source.open("rb") as source_stream, os.fdopen(
                descriptor, "wb"
            ) as destination_stream:
                shutil.copyfileobj(source_stream, destination_stream)
                destination_stream.flush()
                os.fsync(destination_stream.fileno())
            os.chmod(temporary, stat.S_IMODE(info.st_mode))
            with temporary.open("rb") as stream:
                os.fsync(stream.fileno())
            os.replace(temporary, destination)
            _fsync_directory(destination.parent)
        finally:
            temporary.unlink(missing_ok=True)
    elif stat.S_ISLNK(info.st_mode):
        os.symlink(os.readlink(source), destination)
        _fsync_directory(destination.parent)
    else:
        raise PublishError(f"cannot preserve non-leaf path: {source}")


def _prune_empty_parents(path: Path, stop: Path) -> None:
    current = path
    while current != stop:
        try:
            current.rmdir()
        except OSError:
            return
        _fsync_directory(current.parent)
        current = current.parent


def _quarantine_legacy(
    destination: Path,
    transaction: Path,
    metadata: dict[str, Any],
    new_paths: set[str],
) -> None:
    if metadata["old_format"] != "v1":
        return
    roots = set(_load_legacy_roots(transaction / "old-v1.txt"))
    quarantine = Path(metadata["quarantine"])
    moved = False
    for source, relative in _legacy_tombstone_paths(
        destination, roots, new_paths
    ):
        target = quarantine.joinpath(*relative.parts)
        _move_leaf(source, target)
        _prune_empty_parents(source.parent, destination)
        moved = True
    if moved:
        print(
            f"Fluorine preserved retired legacy runtime files in {quarantine}",
            file=sys.stderr,
        )
    _checkpoint("after-legacy-quarantine")


def _legacy_tombstone_paths(
    destination: Path, roots: set[str], new_paths: set[str]
) -> list[tuple[Path, PurePosixPath]]:
    result: list[tuple[Path, PurePosixPath]] = []
    for pattern in LEGACY_TOMBSTONES:
        if PurePosixPath(pattern).parts[0] not in roots:
            continue
        for source in sorted(destination.glob(pattern)):
            relative_text = _relative_path(destination, source)
            if relative_text in new_paths:
                continue
            relative = _safe_relative(relative_text)
            _assert_directory_chain(destination, relative, create=False)
            info = source.lstat()
            if not (stat.S_ISREG(info.st_mode) or stat.S_ISLNK(info.st_mode)):
                raise PublishError(f"legacy tombstone is not a leaf: {source}")
            result.append((source, relative))
    return result


def _old_v2_entries(transaction: Path) -> list[dict[str, Any]]:
    path = transaction / "old-v2.json"
    return _load_manifest(path)[0] if path.exists() else []


def _directory_contains_only_owned_paths(
    destination: Path,
    directory: Path,
    old_paths: set[str],
    old_prefixes: set[str],
) -> bool:
    pending = [directory]
    while pending:
        current = pending.pop()
        try:
            children = list(os.scandir(current))
        except OSError:
            return False
        for child in children:
            path = Path(child.path)
            relative = _relative_path(destination, path)
            try:
                info = child.stat(follow_symlinks=False)
            except OSError:
                return False
            if stat.S_ISDIR(info.st_mode) and not stat.S_ISLNK(info.st_mode):
                if relative not in old_prefixes:
                    return False
                pending.append(path)
            elif relative not in old_paths:
                return False
    return True


def _preflight_destination(
    destination: Path,
    entries: list[dict[str, Any]],
    old_format: str,
    old_entries: list[dict[str, Any]],
    legacy_roots: list[str],
) -> None:
    _ensure_owned_directory(destination, 0o755)
    old_paths = {entry["path"] for entry in old_entries}
    old_prefixes = {
        str(parent)
        for entry in old_entries
        for parent in PurePosixPath(entry["path"]).parents
        if str(parent) != "."
    }
    legacy_root_set = set(legacy_roots)
    new_paths = {entry["path"] for entry in entries}
    if old_format == "v1":
        _legacy_tombstone_paths(destination, legacy_root_set, new_paths)
    for old_entry in old_entries:
        if old_entry["path"] in new_paths:
            continue
        old_relative = _safe_relative(old_entry["path"])
        old_target = destination.joinpath(*old_relative.parts)
        try:
            _assert_directory_chain(destination, old_relative, create=False)
        except PublishError as error:
            try:
                old_target.lstat()
            except (FileNotFoundError, NotADirectoryError):
                continue
            raise error
        try:
            old_info = old_target.lstat()
        except (FileNotFoundError, NotADirectoryError):
            continue
        if stat.S_ISDIR(old_info.st_mode) and not stat.S_ISLNK(old_info.st_mode):
            raise PublishError(f"managed leaf became a directory: {old_target}")
    for entry in entries:
        relative = _safe_relative(entry["path"])
        current = destination
        scheduled_parent_removal = False
        for index, part in enumerate(relative.parts[:-1], start=1):
            current /= part
            try:
                info = current.lstat()
            except FileNotFoundError:
                break
            if not stat.S_ISDIR(info.st_mode) or stat.S_ISLNK(info.st_mode):
                parent_relative = PurePosixPath(*relative.parts[:index]).as_posix()
                if (
                    old_format == "v2"
                    and parent_relative in old_paths
                    and parent_relative not in new_paths
                ):
                    scheduled_parent_removal = True
                    break
                raise PublishError(f"destination has an unsafe parent: {current}")
        if scheduled_parent_removal:
            continue
        target = destination.joinpath(*relative.parts)
        try:
            info = target.lstat()
        except (FileNotFoundError, NotADirectoryError):
            continue
        if _entry_matches(destination, entry):
            continue
        if old_format == "empty":
            raise PublishError(f"unowned path conflicts with bundle leaf: {target}")
        if old_format == "v2" and entry["path"] not in old_paths:
            if not (
                stat.S_ISDIR(info.st_mode)
                and entry["path"] in old_prefixes
                and _directory_contains_only_owned_paths(
                    destination, target, old_paths, old_prefixes
                )
            ):
                raise PublishError(f"unowned path conflicts with bundle leaf: {target}")
        if (
            old_format == "v1"
            and relative.parts[0] not in legacy_root_set
            and entry["path"] != LEGACY_MANIFEST_NAME
        ):
            raise PublishError(f"unowned legacy path conflicts with bundle leaf: {target}")
        if stat.S_ISDIR(info.st_mode):
            owned_transition = (
                old_format == "v2"
                and entry["path"] in old_prefixes
                and _directory_contains_only_owned_paths(
                    destination, target, old_paths, old_prefixes
                )
            )
            if any(target.iterdir()) and not owned_transition:
                raise PublishError(
                    f"non-empty directory conflicts with bundle leaf: {target}"
                )
        elif not (stat.S_ISREG(info.st_mode) or stat.S_ISLNK(info.st_mode)):
            raise PublishError(f"unsupported destination object: {target}")


def _backup_stale_v2(
    destination: Path,
    transaction: Path,
    quarantine: Path,
    old_entries: list[dict[str, Any]],
    new_paths: set[str],
) -> None:
    for entry in old_entries:
        relative_text = entry["path"]
        if relative_text in new_paths:
            continue
        relative = _safe_relative(relative_text)
        source = destination.joinpath(*relative.parts)
        try:
            _assert_directory_chain(destination, relative, create=False)
        except PublishError as error:
            try:
                source.lstat()
            except (FileNotFoundError, NotADirectoryError):
                continue
            raise error
        try:
            source.lstat()
        except (FileNotFoundError, NotADirectoryError):
            continue
        if source.is_dir() and not source.is_symlink():
            raise PublishError(f"managed leaf became a directory: {source}")
        if _entry_matches(destination, entry):
            backup = transaction / "backup" / "stale" / Path(*relative.parts)
            _move_leaf(source, backup)
        else:
            preserved = quarantine / "modified-v2" / Path(*relative.parts)
            _move_leaf(source, preserved)
        _prune_empty_parents(source.parent, destination)
    _checkpoint("after-stale-backup")


def _publish_entry(
    stage: Path,
    destination: Path,
    metadata: dict[str, Any],
    entry: dict[str, Any],
    old_by_path: dict[str, dict[str, Any]],
) -> None:
    if _entry_matches(destination, entry):
        return
    relative = _safe_relative(entry["path"])
    target = destination.joinpath(*relative.parts)
    if target.exists() or target.is_symlink():
        if not target.is_dir() or target.is_symlink():
            preserve = metadata["old_format"] == "v1"
            old_entry = old_by_path.get(entry["path"])
            if old_entry is not None and not _entry_matches(destination, old_entry):
                preserve = True
            if preserve:
                quarantine = (
                    Path(metadata["quarantine"])
                    / "replaced"
                    / Path(*relative.parts)
                )
                _preserve_leaf(target, quarantine)
    _copy_entry(
        stage,
        destination,
        entry,
        transaction_id=metadata["bundle_id"],
    )
    _checkpoint(f"after-entry:{entry['path']}")


def _checkpoint(name: str) -> None:
    if os.environ.get("FLUORINE_PUBLISH_KILLPOINT") == name:
        os.kill(os.getpid(), signal.SIGKILL)
    if os.environ.get("FLUORINE_PUBLISH_FAILPOINT") == name:
        raise PublishError(f"injected publication failure at {name}")


def _installed_commit_matches(destination: Path, bundle_id: str) -> bool:
    manifest = destination / MANIFEST_NAME
    marker = destination / MARKER_NAME
    return (
        marker.is_file()
        and marker.read_bytes() == f"manifest:{bundle_id}\n".encode("ascii")
        and manifest.is_file()
        and _sha256_file(manifest) == bundle_id
    )


def _cleanup_transaction_temporaries(
    destination: Path, entries: list[dict[str, Any]], bundle_id: str
) -> None:
    suffix_length = 16 + 1 + 16
    prefix_token = f"{bundle_id[:16]}-"
    parents = {
        destination.joinpath(*_safe_relative(entry["path"]).parts).parent
        for entry in entries
    }
    for parent in parents:
        if not parent.is_dir() or parent.is_symlink():
            continue
        for path in parent.glob(f".*.fluorine-{prefix_token}*"):
            suffix = path.name.rsplit(".fluorine-", 1)[-1]
            if len(suffix) != suffix_length or not suffix.startswith(prefix_token):
                continue
            random_part = suffix[len(prefix_token) :]
            if not all(character in "0123456789abcdef" for character in random_part):
                continue
            info = path.lstat()
            if not (stat.S_ISREG(info.st_mode) or stat.S_ISLNK(info.st_mode)):
                raise PublishError(f"publication temporary path is unsafe: {path}")
            path.unlink()
            _fsync_directory(parent)


def _retire_directory(path: Path, parent: Path) -> None:
    if not path.exists():
        return
    retired = parent / f".{path.name}.retired-{secrets.token_hex(8)}"
    os.rename(path, retired)
    _fsync_directory(parent)
    shutil.rmtree(retired, ignore_errors=True)
    _fsync_directory(parent)


def _write_recovery_pointer(data_root: Path, bundle_id: str) -> None:
    _atomic_write(
        data_root / RECOVERY_NAME,
        f"{bundle_id}\n".encode("ascii"),
        0o600,
    )


def _recovery_stage(data_root: Path) -> tuple[Path, str] | None:
    pointer = data_root / RECOVERY_NAME
    if not pointer.exists() and not pointer.is_symlink():
        return None
    try:
        info = pointer.lstat()
        raw = pointer.read_text(encoding="ascii")
    except (OSError, UnicodeError) as error:
        raise PublishError(f"invalid publication recovery pointer: {error}") from error
    bundle_id = raw.removesuffix("\n")
    if (
        not stat.S_ISREG(info.st_mode)
        or stat.S_ISLNK(info.st_mode)
        or len(bundle_id) != 64
    ):
        raise PublishError("invalid publication recovery pointer")
    try:
        int(bundle_id, 16)
    except ValueError as error:
        raise PublishError("invalid publication recovery pointer") from error
    stage = data_root / "publish-stage" / bundle_id
    try:
        stage_identity = validate_bundle(stage)
    except (OSError, PublishError) as error:
        raise PublishError(
            f"publication recovery stage is missing or invalid: {error}"
        ) from error
    if stage_identity != bundle_id:
        raise PublishError("publication recovery stage is missing or invalid")
    return stage, bundle_id


def _clear_recovery_pointer(data_root: Path, bundle_id: str) -> None:
    recovery = _recovery_stage(data_root)
    if recovery is None:
        return
    _, current_id = recovery
    if current_id != bundle_id:
        raise PublishError("publication recovery pointer changed unexpectedly")
    pointer = data_root / RECOVERY_NAME
    retired = data_root / f".{RECOVERY_NAME}.retired-{secrets.token_hex(8)}"
    os.rename(pointer, retired)
    _fsync_directory(data_root)
    retired.unlink(missing_ok=True)
    _fsync_directory(data_root)


def _finish_transaction(
    data_root: Path, transaction: Path, stage: Path, bundle_id: str
) -> None:
    _retire_directory(transaction, data_root)
    _clear_recovery_pointer(data_root, bundle_id)
    _retire_directory(stage, stage.parent)


def _bootstrap_launcher(
    stage: Path,
    destination: Path,
    metadata: dict[str, Any],
    old_format: str,
    old_entries: list[dict[str, Any]],
    legacy_roots: list[str],
    bundle_id: str,
) -> None:
    entries, _ = _load_manifest(stage / MANIFEST_NAME)
    launcher = next(
        (entry for entry in entries if entry["path"] == "fluorine-manager"), None
    )
    if launcher is None or launcher["type"] != "file":
        raise PublishError("bundle does not contain a regular Fluorine launcher")
    target = destination / "fluorine-manager"
    if target.exists() or target.is_symlink():
        if target.is_dir() and not target.is_symlink():
            raise PublishError("installed Fluorine launcher became a directory")
        if old_format == "v2" and "fluorine-manager" not in {
            entry["path"] for entry in old_entries
        }:
            raise PublishError("installed Fluorine launcher is not owned by its manifest")
        if old_format == "v1" and "fluorine-manager" not in legacy_roots:
            raise PublishError("legacy Fluorine launcher is not owned by its manifest")
        if old_format == "empty":
            raise PublishError("unowned Fluorine launcher blocks publication")
        old_entry = next(
            (
                entry
                for entry in old_entries
                if entry["path"] == "fluorine-manager"
            ),
            None,
        )
        should_preserve = old_format == "v1" or (
            old_entry is not None
            and not _entry_matches(destination, old_entry)
        )
        if should_preserve and not _entry_matches(destination, launcher):
            _preserve_leaf(
                target,
                Path(metadata["quarantine"])
                / "replaced"
                / "fluorine-manager",
            )
    _copy_entry(stage, destination, launcher, transaction_id=bundle_id)


def _apply_transaction(destination: Path, data_root: Path, transaction: Path) -> None:
    metadata = _transaction_metadata(transaction)
    if Path(metadata["destination"]).absolute() != destination.absolute():
        raise PublishError("publication transaction belongs to a different installation")
    stage = Path(metadata["stage"])
    bundle_id = metadata["bundle_id"]
    if validate_bundle(stage) != bundle_id:
        raise PublishError("publication stage no longer matches its transaction")

    installed_manifest = destination / MANIFEST_NAME
    marker = destination / MARKER_NAME
    expected_marker = f"manifest:{bundle_id}\n".encode("ascii")
    if marker.is_file() and marker.read_bytes() == expected_marker:
        if installed_manifest.is_file() and _sha256_file(installed_manifest) == bundle_id:
            _finish_transaction(data_root, transaction, stage, bundle_id)
            return
        raise PublishError("installed marker names a missing or different manifest")

    entries, manifest_raw = _load_manifest(stage / MANIFEST_NAME)
    _cleanup_transaction_temporaries(destination, entries, bundle_id)
    new_paths = {entry["path"] for entry in entries}
    old_entries = _old_v2_entries(transaction)
    old_by_path = {entry["path"]: entry for entry in old_entries}
    legacy_roots = (
        _load_legacy_roots(transaction / "old-v1.txt")
        if metadata["old_format"] == "v1"
        else []
    )
    _quarantine_legacy(destination, transaction, metadata, new_paths)
    _backup_stale_v2(
        destination,
        transaction,
        Path(metadata["quarantine"]),
        old_entries,
        new_paths,
    )
    _preflight_destination(
        destination,
        entries,
        metadata["old_format"],
        old_entries,
        legacy_roots,
    )

    priority = {"fluorine-publisher.py": 0, "fluorine-manager": 1}
    for entry in sorted(entries, key=lambda item: (priority.get(item["path"], 2), item["path"])):
        _publish_entry(stage, destination, metadata, entry, old_by_path)

    for entry in entries:
        if not _entry_matches(destination, entry):
            raise PublishError(f"installed entry failed final verification: {entry['path']}")
    _atomic_write(installed_manifest, manifest_raw)
    _checkpoint("after-manifest")
    _atomic_write(marker, expected_marker)
    _checkpoint("after-marker")
    _finish_transaction(data_root, transaction, stage, bundle_id)


@dataclass
class PublicationLock:
    data_root: Path

    def __enter__(self) -> PublicationLock:
        _ensure_owned_directory(self.data_root)
        self._stream = (self.data_root / "publish.lock").open("a+b")
        fcntl.flock(self._stream.fileno(), fcntl.LOCK_EX)
        return self

    def __exit__(self, *_: object) -> None:
        fcntl.flock(self._stream.fileno(), fcntl.LOCK_UN)
        self._stream.close()


def _cleanup_abandoned_publication_artifacts(
    data_root: Path, preserve_stage: Path | None = None
) -> None:
    for pattern in (
        ".publish-transaction-*",
        ".publish-transaction.retired-*",
        ".publish-recovery.retired-*",
    ):
        for path in data_root.glob(pattern):
            if path.is_dir() and not path.is_symlink():
                shutil.rmtree(path, ignore_errors=True)
            elif path.exists() or path.is_symlink():
                path.unlink(missing_ok=True)
    stage_root = data_root / "publish-stage"
    if stage_root.is_dir() and not stage_root.is_symlink():
        keep: set[Path] = set()
        if preserve_stage is not None:
            keep.add(preserve_stage.absolute())
        pointer = data_root / RECOVERY_NAME
        if pointer.is_file() and not pointer.is_symlink():
            try:
                identity = pointer.read_text(encoding="ascii").removesuffix("\n")
            except (OSError, UnicodeError):
                identity = ""
            if len(identity) == 64 and all(
                character in "0123456789abcdef" for character in identity
            ):
                keep.add((stage_root / identity).absolute())
        transaction = data_root / "publish-transaction"
        if not transaction.exists():
            for path in stage_root.iterdir():
                name = path.name
                is_stage = len(name) == 64 and all(
                    character in "0123456789abcdef" for character in name
                )
                is_retired_stage = (
                    name.startswith(".")
                    and ".retired-" in name
                    and len(name.split(".retired-", 1)[0]) == 65
                    and all(
                        character in "0123456789abcdef"
                        for character in name[1:].split(".retired-", 1)[0]
                    )
                )
                if (
                    path.absolute() not in keep
                    and (
                        name.startswith(".incoming-")
                        or is_stage
                        or is_retired_stage
                    )
                    and path.is_dir()
                    and not path.is_symlink()
                ):
                    if is_retired_stage:
                        shutil.rmtree(path, ignore_errors=True)
                        _fsync_directory(stage_root)
                    else:
                        _retire_directory(path, stage_root)
    _fsync_directory(data_root)


@dataclass
class RuntimePublicationLock:
    data_root: Path
    wait_seconds: float = 0

    def __enter__(self) -> RuntimePublicationLock:
        self._stream = (self.data_root / "runtime.lock").open("a+b")
        deadline = time.monotonic() + self.wait_seconds
        while True:
            try:
                fcntl.flock(
                    self._stream.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB
                )
                break
            except BlockingIOError as error:
                if time.monotonic() >= deadline:
                    self._stream.close()
                    raise PublishError(
                        "Fluorine is running; close it before publishing an update"
                    ) from error
                time.sleep(0.1)
        return self

    def __exit__(self, *_: object) -> None:
        fcntl.flock(self._stream.fileno(), fcntl.LOCK_UN)
        self._stream.close()


def _begin_transaction(
    destination: Path, data_root: Path, stage: Path, bundle_id: str
) -> None:
    installation = _inspect_installation(destination, stage)
    old_format, old_entries, _, legacy_roots = installation
    entries, _ = _load_manifest(stage / MANIFEST_NAME)
    _preflight_destination(
        destination, entries, old_format, old_entries, legacy_roots
    )
    _write_recovery_pointer(data_root, bundle_id)
    transaction = _create_transaction(
        data_root, destination, stage, bundle_id, installation
    )
    metadata = _transaction_metadata(transaction)
    # The launcher is the admission/recovery boundary. Publish it atomically
    # before any other live leaf changes; it invokes the immutable staged
    # Python when the recovery pointer exists.
    _bootstrap_launcher(
        stage,
        destination,
        metadata,
        old_format,
        old_entries,
        legacy_roots,
        bundle_id,
    )
    _apply_transaction(destination, data_root, transaction)


def _resume_recovery(
    destination: Path, data_root: Path, wait_runtime: float
) -> None:
    recovery = _recovery_stage(data_root)
    transaction = data_root / "publish-transaction"
    if recovery is None:
        if transaction.exists():
            raise PublishError("publication transaction has no recovery stage pointer")
        return
    stage, bundle_id = recovery
    with RuntimePublicationLock(data_root, wait_runtime):
        if transaction.exists():
            _apply_transaction(destination, data_root, transaction)
        elif _installed_commit_matches(destination, bundle_id):
            _clear_recovery_pointer(data_root, bundle_id)
            _retire_directory(stage, stage.parent)
        else:
            _begin_transaction(destination, data_root, stage, bundle_id)


def publish(
    source: Path,
    destination: Path,
    data_root: Path,
    wait_runtime: float = 0,
) -> str:
    source = source.resolve(strict=True)
    destination = destination.absolute()
    data_root = data_root.absolute()
    _, source_manifest = _load_manifest(source / MANIFEST_NAME)
    source_id = _sha256_bytes(source_manifest)
    with PublicationLock(data_root):
        _cleanup_abandoned_publication_artifacts(data_root, source)
        if (data_root / RECOVERY_NAME).exists() or (
            data_root / "publish-transaction"
        ).exists():
            _ensure_owned_directory(destination, 0o755)
        _resume_recovery(destination, data_root, wait_runtime)
        if _installed_commit_matches(destination, source_id):
            return source_id
        stage, bundle_id = _stage_bundle(source, data_root)
        _ensure_owned_directory(destination, 0o755)
        try:
            with RuntimePublicationLock(data_root, wait_runtime):
                _begin_transaction(destination, data_root, stage, bundle_id)
        except Exception:
            if _recovery_stage(data_root) is None:
                _retire_directory(stage, stage.parent)
            raise
        return bundle_id


def check_installed(destination: Path, data_root: Path) -> str:
    destination = destination.absolute()
    data_root = data_root.absolute()
    with PublicationLock(data_root):
        _cleanup_abandoned_publication_artifacts(data_root)
        _resume_recovery(destination, data_root, 0)
        manifest = destination / MANIFEST_NAME
        marker = destination / MARKER_NAME
        if not manifest.is_file() or not marker.is_file():
            raise PublishError(
                "installed Fluorine bundle is incomplete; run a release launcher to repair it"
            )
        bundle_id = _sha256_file(manifest)
        if marker.read_bytes() != f"manifest:{bundle_id}\n".encode("ascii"):
            raise PublishError(
                "installed Fluorine marker does not match its manifest; run a release launcher to repair it"
            )
        return bundle_id


def verify_committed(destination: Path, data_root: Path) -> str:
    """Read-only admission check, called while the launcher holds runtime.lock."""
    destination = destination.absolute()
    data_root = data_root.absolute()
    if (data_root / RECOVERY_NAME).exists() or (
        data_root / "publish-transaction"
    ).exists():
        raise PublishError(
            "Fluorine publication is incomplete; run the launcher again to recover it"
        )
    manifest = destination / MANIFEST_NAME
    marker = destination / MARKER_NAME
    if not manifest.is_file() or not marker.is_file():
        raise PublishError("installed Fluorine bundle has no committed manifest")
    bundle_id = _sha256_file(manifest)
    if marker.read_bytes() != f"manifest:{bundle_id}\n".encode("ascii"):
        raise PublishError("installed Fluorine marker does not match its manifest")
    return bundle_id


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)
    build = subparsers.add_parser("build-manifest")
    build.add_argument("root", type=Path)
    validate = subparsers.add_parser("validate")
    validate.add_argument("root", type=Path)
    publish_parser = subparsers.add_parser("publish")
    publish_parser.add_argument("source", type=Path)
    publish_parser.add_argument("destination", type=Path)
    publish_parser.add_argument("data_root", type=Path)
    publish_parser.add_argument("--wait-runtime", type=float, default=0)
    check = subparsers.add_parser("check-installed")
    check.add_argument("destination", type=Path)
    check.add_argument("data_root", type=Path)
    verify = subparsers.add_parser("verify-committed")
    verify.add_argument("destination", type=Path)
    verify.add_argument("data_root", type=Path)
    arguments = parser.parse_args(argv)
    try:
        if arguments.command == "build-manifest":
            print(build_manifest(arguments.root))
        elif arguments.command == "validate":
            print(validate_bundle(arguments.root))
        elif arguments.command == "publish":
            print(
                publish(
                    arguments.source,
                    arguments.destination,
                    arguments.data_root,
                    arguments.wait_runtime,
                )
            )
        elif arguments.command == "check-installed":
            print(check_installed(arguments.destination, arguments.data_root))
        else:
            print(verify_committed(arguments.destination, arguments.data_root))
    except PublishError as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

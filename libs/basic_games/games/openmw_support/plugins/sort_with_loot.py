"""
sort_with_loot.py — an optional "Sort with LOOT" tool for OpenMW.

This is an ``IPluginTool``, but it does not live in the Tools menu: the game
declares it via ``sortToolName()`` and Fluorine's regular Sort button on the
Plugins tab — the same button every other game uses — invokes it, while the
core hides it from the Tools menu so sorting exists in exactly one place. For
other games that button downloads the *Windows* LOOT.exe and runs it under
Proton on the merged VFS, which cannot work for a native-Linux, VFS-less
OpenMW setup; this tool instead drives the native ``libloot`` Python bindings
(module name ``loot``) directly, so sorting stays native and fully optional.

Pipeline it slots into:

    modlist (Fluorine)
        -> Plugins tab (Fluorine, persistent load order)
        -> [optional] Sort button -> *this tool* reorders the tab with LOOT
        -> openmw.cfg content= lines written at launch (game_openmw.py)

Safety contract: this tool must NEVER corrupt the load order. Every libloot
call is guarded; on *any* failure we show the reason and leave the Plugins tab
exactly as it was. The tab is only ever rewritten from a LOOT result whose
plugin set is identical to the active set we handed in.

libloot is an optional native dependency. If the ``loot`` module is not bundled
(e.g. the wheel failed to build for this platform) the tool degrades to a clear
message instead of raising at import time.
"""

from __future__ import annotations

import hashlib
import json
import os
import shutil
import subprocess
import tempfile
import time
import urllib.request
from collections import deque
from dataclasses import dataclass, replace
from pathlib import Path
from typing import Callable, Iterable, Mapping, Sequence

from PyQt6.QtCore import (
    QCoreApplication,
    QObject,
    Qt,
    QThread,
    pyqtSignal,
    qInfo,
    qWarning,
)
from PyQt6.QtWidgets import QMessageBox, QProgressDialog

import mobase

from .. import openmw_cfg as _openmw_cfg

# libloot is optional: probe for it without exploding the whole plugin import if
# the native wheel is missing. The real module is registered as ``loot``.
#
# loot.so STATICALLY links its own libstdc++/boost and dynamically needs only
# libc/libgcc_s. When imported into Mod Organizer's process (which dynamically
# links Qt's libstdc++.so.6), default ELF symbol interposition binds some of
# libloot's C++ runtime symbols to MO's libstdc++ — a cross-runtime mismatch
# that corrupts memory inside libloot's parallel plugin loader and segfaults
# (load_plugins crashes in-process but runs fine in a bare Python). Loading the
# extension with RTLD_DEEPBIND makes it prefer its own self-contained symbols
# first while still resolving the Python C-API from the global scope (loot.so
# does not link libpython), which eliminates the clash. Harmless standalone.
try:
    import sys as _sys

    _prev_dlopen_flags = None
    try:
        _deepbind = getattr(os, "RTLD_DEEPBIND", 0)
        if _deepbind:
            _prev_dlopen_flags = _sys.getdlopenflags()
            _sys.setdlopenflags(_prev_dlopen_flags | _deepbind)
    except Exception:
        _prev_dlopen_flags = None

    try:
        import loot as _loot  # type: ignore

        _LOOT_AVAILABLE = True
        _LOOT_IMPORT_ERROR = ""
    finally:
        if _prev_dlopen_flags is not None:
            _sys.setdlopenflags(_prev_dlopen_flags)
except Exception as exc:  # pragma: no cover - depends on bundled wheel
    _loot = None  # type: ignore
    _LOOT_AVAILABLE = False
    _LOOT_IMPORT_ERROR = str(exc)


# Directory holding the bundled ``loot`` package, so a child interpreter can find
# it via sys.path (see the subprocess pipeline below).
_LOOT_SITE_PACKAGES = ""


def _loot_module_search_path(module_file: str) -> str:
    path = Path(module_file).resolve()
    return str(path.parent.parent if path.name == "__init__.py" else path.parent)


try:
    if _loot is not None and getattr(_loot, "__file__", None):
        _LOOT_SITE_PACKAGES = _loot_module_search_path(_loot.__file__)
except Exception:
    _LOOT_SITE_PACKAGES = ""


# Standalone script run in a CLEAN child interpreter to drive libloot. We do NOT
# call libloot in Mod Organizer's own process: its load_plugins deadlocks there
# (its statically-linked C++ runtime / internal parallel loader does not
# cooperate with MO's already-loaded libraries), even though the exact same call
# runs in well under a second in a bare Python. Isolating it in a child process
# sidesteps that entirely and lets us enforce a hard timeout, so the UI can never
# freeze. Protocol: argv[1]=request.json (inputs), argv[2]=response.json (result).
_LOOT_SUBPROCESS_SRC = r'''
import sys, os, json, shutil, tempfile

def main():
    req_path, resp_path = sys.argv[1], sys.argv[2]
    with open(req_path, encoding="utf-8") as fh:
        req = json.load(fh)
    resp = {"sorted": None, "error": ""}
    try:
        sp = req.get("site_packages")
        if sp and sp not in sys.path:
            sys.path.insert(0, sp)
        import loot as L
        gt = getattr(L.GameType, "OpenMW")
        game_path = req["game_path"]
        local_path = tempfile.mkdtemp(prefix="fluorine_loot_state_")
        try:
            with open(os.path.join(local_path, "openmw.cfg"), "w", encoding="utf-8") as cfg:
                for plugin in req.get("condition_active", []):
                    cfg.write("content=%s\n" % plugin)
            game = L.Game(gt, game_path, local_path)
            game.set_additional_data_paths(list(req.get("data_dirs", [])))
            masterlist = req.get("masterlist")
            if masterlist:
                game.database().load_masterlist(masterlist)
            game.load_current_load_order_state()
            game.load_plugins(list(req["plugin_paths"]))
            resp["sorted"] = list(game.sort_plugins(list(req["active"])))
        finally:
            shutil.rmtree(local_path, ignore_errors=True)
    except Exception as exc:
        resp["error"] = "%s: %s" % (type(exc).__name__, exc)
    with open(resp_path, "w", encoding="utf-8") as fh:
        json.dump(resp, fh)

main()
'''


# OpenMW shares Morrowind's LOOT masterlist. This is intentionally pinned to the
# commit at the v0.26 branch tip on 2025-12-09. Updating either value is a
# separate, tested dependency update; sparse public metadata does not fully
# describe curated lists such as NEMAS.
_MASTERLIST_COMMIT = "748db08e41c2bfae6c82e9d4529b796b2df80666"
_DEFAULT_MASTERLIST_URL = (
    "https://raw.githubusercontent.com/loot/morrowind/"
    f"{_MASTERLIST_COMMIT}/masterlist.yaml"
)
_DEFAULT_MASTERLIST_SHA256 = (
    "ffd36bc08df55565e9ad5f179b72ffe53e4404c06ec8c46773defe354a2f69c6"
)
# Re-download an unpinned override when its cache is older than this many seconds.
_MASTERLIST_MAX_AGE = 24 * 60 * 60
# Kezyma "OpenMW Player" stub esps — never feed these to LOOT.
_STUB_SUFFIXES = (".omwaddon.esp", ".omwscripts.esp", ".omwgame.esp")


@dataclass(frozen=True)
class PluginSlot:
    """One canonical state slot used by the non-destructive LOOT merge."""

    name: str
    active: bool
    available: bool = True
    primary: bool = False
    groundcover: bool = False
    required_masters: tuple[str, ...] = ()

    @property
    def wrapper(self) -> bool:
        return self.name.casefold().endswith(_STUB_SUFFIXES)

    @property
    def omwscripts(self) -> bool:
        return self.name.casefold().endswith(".omwscripts")

    @property
    def movable(self) -> bool:
        return (
            self.active
            and self.available
            and not self.primary
            and not self.groundcover
            and not self.omwscripts
            and not self.wrapper
        )

    @property
    def submitted_to_loot(self) -> bool:
        return self.active and self.available and (self.primary or self.movable)


@dataclass(frozen=True)
class LootMerge:
    order: tuple[str, ...]
    request: tuple[str, ...]
    moved: int


@dataclass(frozen=True)
class ResourceSelection:
    root: Path
    installation: str


_OPENMW_PATH_TOKENS = ("?local?", "?userconfig?", "?userdata?", "?global?")


def _resolve_openmw_path(
    value: str, cfg_path: Path, token_roots: Mapping[str, Path]
) -> Path:
    """Resolve one OpenMW path in the context of its config source."""
    parsed = _openmw_cfg.parse_openmw_path(value.strip())
    if not parsed:
        raise ValueError(f"Empty OpenMW path in {cfg_path}")
    for token in _OPENMW_PATH_TOKENS:
        if not parsed.startswith(token):
            continue
        root = token_roots.get(token)
        if root is None:
            raise ValueError(f"Unresolved OpenMW path token {token} in {cfg_path}")
        suffix = parsed[len(token) :].lstrip("/\\")
        return (root / suffix).expanduser().resolve(strict=False)
    if parsed.startswith("?"):
        raise ValueError(f"Unknown OpenMW path token in {cfg_path}: {parsed!r}")

    path = Path(parsed).expanduser()
    if not path.is_absolute():
        path = cfg_path.parent / path
    return path.resolve(strict=False)


def _read_resource_source(
    cfg_path: Path, token_roots: Mapping[str, Path]
) -> tuple[Path | None, list[Path], bool]:
    resource: Path | None = None
    config_dirs: list[Path] = []
    replace_config = False
    lines = cfg_path.read_text(encoding="utf-8", errors="replace").splitlines()
    for raw in lines:
        line = raw.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        raw_key, raw_value = line.split("=", 1)
        key = raw_key.strip().casefold()
        value = raw_value.strip()
        if key == "replace":
            replace_config = replace_config or any(
                target.casefold() == "config" for target in value.split()
            )
        elif key == "config" and value:
            config_dirs.append(_resolve_openmw_path(value, cfg_path, token_roots))
        elif key == "resources" and value:
            resource = _resolve_openmw_path(value, cfg_path, token_roots)
    return resource, config_dirs, replace_config


def _effective_resource_from_cfg(
    cfg_path: Path, token_roots: Mapping[str, Path]
) -> Path | None:
    """Return the effective scalar ``resources`` value for one config chain."""
    root = cfg_path.expanduser().resolve(strict=False)
    pending = deque([(root, frozenset())])
    discovered: set[Path] = set()
    sources: list[Path | None] = []

    while pending:
        path, ancestors = pending.popleft()
        path = path.resolve(strict=False)
        if path in ancestors:
            raise ValueError(f"Cycle in OpenMW config chain at {path}")
        if path in discovered:
            raise ValueError(f"Repeated OpenMW config source: {path}")
        discovered.add(path)
        if not path.is_file():
            continue

        resource, config_dirs, replace_config = _read_resource_source(
            path, token_roots
        )
        if replace_config and sources:
            # The base local/global source is not itself selected by config=.
            sources[1:] = []
            pending.clear()
            discovered = {root, path}
        sources.append(resource)
        next_ancestors = ancestors | {path}
        pending.extend(
            (directory / "openmw.cfg", next_ancestors)
            for directory in config_dirs
        )

    return next((value for value in reversed(sources) if value is not None), None)


def _native_token_roots(executable_dir: Path, prefix: Path) -> dict[str, Path]:
    config_home = Path(
        os.environ.get("XDG_CONFIG_HOME", str(Path.home() / ".config"))
    ).expanduser()
    data_home = Path(
        os.environ.get("XDG_DATA_HOME", str(Path.home() / ".local/share"))
    ).expanduser()
    return {
        "?local?": executable_dir,
        "?userconfig?": config_home / "openmw",
        "?userdata?": data_home / "openmw",
        "?global?": prefix / "share/games/openmw",
    }


def _flatpak_token_roots(deployment: Path | None) -> dict[str, Path]:
    app_home = Path.home() / ".var/app/org.openmw.OpenMW"
    roots = {
        "?userconfig?": app_home / "config/openmw",
        "?userdata?": app_home / "data/openmw",
    }
    if deployment is not None:
        files = deployment / "files"
        roots["?local?"] = files / "bin"
        roots["?global?"] = files / "share/games/openmw"
    return roots


def _casefold_index(names: Iterable[str], label: str) -> dict[str, str]:
    index: dict[str, str] = {}
    for name in names:
        key = name.casefold()
        if key in index:
            raise ValueError(
                f"{label} contains duplicate or casing-ambiguous plugin "
                f"identities: '{index[key]}' and '{name}'."
            )
        index[key] = name
    return index


def loot_sort_request(slots: Sequence[PluginSlot]) -> tuple[str, ...]:
    request = tuple(slot.name for slot in slots if slot.submitted_to_loot)
    if not request:
        raise ValueError("There are no movable active plugins to sort with LOOT.")
    _casefold_index(request, "The LOOT request")
    return request


def validate_required_masters(slots: Sequence[PluginSlot]) -> None:
    """Reject missing, unavailable, or inactive masters before invoking LOOT."""
    by_name = _casefold_index((slot.name for slot in slots), "The plugin state")
    states = {slot.name.casefold(): slot for slot in slots}
    for slot in slots:
        if not slot.movable:
            continue
        for master in slot.required_masters:
            master_slot = states.get(master.casefold())
            if master_slot is None:
                raise ValueError(
                    f"'{slot.name}' requires missing master '{master}'."
                )
            if not master_slot.available:
                raise ValueError(
                    f"'{slot.name}' requires unavailable master "
                    f"'{by_name[master.casefold()]}'."
                )
            if not master_slot.active:
                raise ValueError(
                    f"'{slot.name}' requires inactive master "
                    f"'{by_name[master.casefold()]}'."
                )


def merge_active_slots(
    slots: Sequence[PluginSlot],
    sorted_request: Sequence[str],
    primary_order: Sequence[str],
) -> LootMerge:
    """Merge a validated LOOT result into movable active slots only."""
    validate_required_masters(slots)
    request = loot_sort_request(slots)
    request_index = _casefold_index(request, "The LOOT request")
    result_index = _casefold_index(sorted_request, "The LOOT result")
    if len(sorted_request) != len(request) or set(result_index) != set(request_index):
        raise ValueError(
            "LOOT returned a plugin set that does not exactly match the request."
        )

    canonical_result = tuple(request_index[name.casefold()] for name in sorted_request)
    primary_keys = {name.casefold() for name in primary_order}
    for slot in slots:
        if slot.primary != (slot.name.casefold() in primary_keys):
            raise ValueError(
                f"Primary classification for '{slot.name}' does not match the "
                "canonical primary list."
            )
    request_primaries = tuple(
        name for name in request if name.casefold() in primary_keys
    )
    expected_primaries = tuple(
        request_index[name.casefold()]
        for name in primary_order
        if name.casefold() in request_index
    )
    if request_primaries != expected_primaries:
        raise ValueError("Primary plugins are not in canonical primary order.")

    for index, name in enumerate(request):
        if (
            name.casefold() in primary_keys
            and canonical_result[index].casefold() != name.casefold()
        ):
            raise ValueError(f"LOOT attempted to move fixed primary plugin '{name}'.")

    movable_result = iter(
        name for name in canonical_result if name.casefold() not in primary_keys
    )
    merged: list[str] = []
    moved = 0
    for slot in slots:
        if slot.movable:
            replacement = next(movable_result)
            merged.append(replacement)
            if replacement.casefold() != slot.name.casefold():
                moved += 1
        else:
            merged.append(slot.name)
    try:
        next(movable_result)
    except StopIteration:
        pass
    else:  # defensive: request/result validation should make this unreachable
        raise ValueError("LOOT returned too many movable plugins.")

    movable_indices = [index for index, slot in enumerate(slots) if slot.movable]
    for barrier, script in (
        (index, slot.name)
        for index, slot in enumerate(slots)
        if slot.name.casefold().endswith(".omwscripts")
    ):
        before = {
            slots[index].name.casefold()
            for index in movable_indices
            if index < barrier
        }
        after = {
            merged[index].casefold()
            for index in movable_indices
            if index < barrier
        }
        if before != after:
            raise ValueError(
                f"LOOT attempted to move plugins across fixed script slot "
                f"{script!r}."
            )

    positions = {name.casefold(): index for index, name in enumerate(merged)}
    for slot in slots:
        if not slot.movable:
            continue
        for master in slot.required_masters:
            if positions[master.casefold()] > positions[slot.name.casefold()]:
                raise ValueError(
                    f"LOOT placed '{slot.name}' before required master '{master}'."
                )
    return LootMerge(tuple(merged), request, moved)


def _normalize_resource_root(candidate: Path) -> Path:
    candidate = candidate.expanduser().resolve(strict=False)
    return candidate.parent if candidate.name.casefold() == "resources" else candidate


def _valid_resource_root(candidate: Path) -> Path | None:
    root = _normalize_resource_root(candidate)
    required = root / "resources" / "vfs"
    if (
        root.is_dir()
        and required.is_dir()
        and os.access(root, os.R_OK | os.X_OK)
        and os.access(required, os.R_OK | os.X_OK)
    ):
        return root.resolve()
    return None


def select_resource_root(
    override: str,
    native_candidates: Sequence[Path],
    flatpak_candidates: Sequence[Path],
) -> ResourceSelection:
    """Select one host-readable libloot OpenMW resource root."""
    if override.strip():
        selected = _valid_resource_root(Path(override.strip()))
        if selected is None:
            raise ValueError(
                "The configured openmw_install_path is invalid or unreadable; it "
                "must be an install root (or resources directory) containing "
                "resources/vfs."
            )
        return ResourceSelection(selected, "override")

    def valid_unique(candidates: Sequence[Path]) -> list[Path]:
        roots: dict[str, Path] = {}
        for candidate in candidates:
            root = _valid_resource_root(candidate)
            if root is not None:
                roots.setdefault(os.path.normcase(str(root)), root)
        return list(roots.values())

    native = valid_unique(native_candidates)
    flatpak = valid_unique(flatpak_candidates)
    native_keys = {os.path.normcase(str(root)) for root in native}
    flatpak = [
        root
        for root in flatpak
        if os.path.normcase(str(root)) not in native_keys
    ]
    if len(native) > 1:
        raise ValueError(
            "Multiple native OpenMW resource installations were found; set "
            "openmw_install_path explicitly."
        )
    if len(flatpak) > 1:
        raise ValueError(
            "Multiple Flatpak OpenMW resource installations were found; set "
            "openmw_install_path to a host-visible path explicitly."
        )
    if native and flatpak:
        raise ValueError(
            "Both native and Flatpak OpenMW resources are available; select one "
            "with openmw_install_path. This does not change the launch executable."
        )
    if native:
        return ResourceSelection(native[0], "native")
    if flatpak:
        return ResourceSelection(flatpak[0], "flatpak")
    if flatpak_candidates:
        raise ValueError(
            "Flatpak OpenMW resources are not host-readable; set "
            "openmw_install_path to an explicit host-visible resources path."
        )
    raise ValueError(
        "No valid OpenMW resources installation was found; set "
        "openmw_install_path explicitly."
    )


def _sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _atomic_write_bytes(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        try:
            os.unlink(temporary)
        except FileNotFoundError:
            pass


def ensure_masterlist(
    cache_path: Path,
    url: str,
    expected_sha256: str | None,
    download: bool,
    opener: Callable[..., object] = urllib.request.urlopen,
) -> Path | None:
    """Return a verified cache, updating it atomically when needed."""
    cached_data: bytes | None = None
    try:
        cached_data = cache_path.read_bytes()
    except FileNotFoundError:
        pass
    cache_valid = bool(cached_data) and (
        expected_sha256 is None or _sha256(cached_data or b"") == expected_sha256
    )

    # Immutable pinned content never needs a network refresh once verified.
    fresh = False
    if cache_valid:
        try:
            fresh = (time.time() - cache_path.stat().st_mtime) < _MASTERLIST_MAX_AGE
        except OSError:
            pass
    if cache_valid and (expected_sha256 is not None or fresh or not download):
        return cache_path
    if not download:
        return None

    try:
        with opener(url, timeout=15) as response:  # type: ignore[attr-defined]
            downloaded = response.read()
    except Exception:
        if cache_valid:
            return cache_path
        return None
    if not downloaded:
        if cache_valid:
            return cache_path
        raise RuntimeError("The LOOT masterlist download was empty.")
    if expected_sha256 is not None and _sha256(downloaded) != expected_sha256:
        if cache_valid:
            return cache_path
        raise RuntimeError(
            "The downloaded LOOT masterlist failed SHA-256 verification."
        )
    _atomic_write_bytes(cache_path, downloaded)
    return cache_path


class _LootProgressDialog(QProgressDialog):
    """Modal progress that cannot outlive its non-interruptible worker."""

    def __init__(self, *args, **kwargs):  # noqa: ANN002, ANN003
        super().__init__(*args, **kwargs)
        self._worker_finished = False

    def finishWorker(self) -> None:
        self._worker_finished = True
        self.close()

    def reject(self) -> None:
        if self._worker_finished:
            super().reject()

    def closeEvent(self, event) -> None:  # noqa: ANN001
        if not self._worker_finished:
            event.ignore()
            return
        super().closeEvent(event)


class _LootWorker(QObject):
    """Runs the blocking libloot pipeline off the UI thread.

    It receives only plain data (paths, plugin filenames) — never a mobase or Qt
    object — computes the sorted active order with libloot, and emits it back.
    The caller applies the result on the main thread. This keeps the UI
    responsive and the progress dialog animated while LOOT works.
    """

    progress = pyqtSignal(str)
    # (sorted_active | None, error_text): None + text means "do not touch order".
    finished = pyqtSignal(object, str)

    def __init__(
        self,
        game_path: str,
        local_path: str | None,
        data_dirs: list[str],
        active: list[str],
        condition_active: list[str],
        masterlist_cache: str | None,
        masterlist_url: str,
        masterlist_sha256: str | None,
        masterlist_download: bool,
    ) -> None:
        super().__init__()
        self._game_path = game_path
        self._local_path = local_path
        self._data_dirs = data_dirs
        self._active = active
        self._condition_active = condition_active
        self._ml_cache = masterlist_cache
        self._ml_url = masterlist_url
        self._ml_sha256 = masterlist_sha256
        self._ml_download = masterlist_download
        # Result, stashed here so the main thread can read it after join. The
        # finished signal is only used to stop the thread/close the dialog.
        self.result_sorted: list[str] | None = None
        self.result_error: str = ""
        self.result_done: bool = False

    def _ensure_masterlist(self) -> str | None:
        if not self._ml_cache:
            return None
        path = Path(self._ml_cache)
        try:
            result = ensure_masterlist(
                path,
                self._ml_url,
                self._ml_sha256,
                self._ml_download,
            )
        except Exception as exc:
            raise RuntimeError(f"Could not update the LOOT masterlist ({exc}).")
        if result is None:
            qWarning(
                "OpenMW LOOT: no verified masterlist is available; continuing "
                "with dependency-only sorting."
            )
            return None
        return str(result)

    def _resolve_plugin_paths(self) -> tuple[list[str], list[str]]:
        """Map each active plugin name to its absolute file path.

        openmw.cfg's data= lines are ascending precedence (the last entry wins),
        so we search them in reverse and take the first hit — matching OpenMW's
        own override rules. Returns (resolved_abs_paths, unresolved_names).
        """
        resolved: list[str] = []
        missing: list[str] = []
        # Pre-list each data dir once (case-insensitive match without a stat per
        # candidate): {dir_index: {lower_name: real_name}}.
        listings: list[dict[str, str]] = []
        for d in self._data_dirs:
            entry_map: dict[str, str] = {}
            try:
                for name in os.listdir(d):
                    entry_map.setdefault(name.lower(), name)
            except OSError:
                pass
            listings.append(entry_map)

        for plugin in self._active:
            key = plugin.lower()
            found: str | None = None
            for idx in range(len(self._data_dirs) - 1, -1, -1):
                real = listings[idx].get(key)
                if real is not None:
                    found = os.path.join(self._data_dirs[idx], real)
                    break
            if found is not None:
                resolved.append(found)
            else:
                missing.append(plugin)
        return resolved, missing

    def run(self) -> None:
        try:
            self.progress.emit(self.tr("Updating the LOOT masterlist..."))
            masterlist = self._ensure_masterlist()

            self.progress.emit(self.tr("Resolving plugin files..."))
            # Absolute plugin paths (not bare names): libloot resolves bare names
            # against the game_path's resources/vfs, the wrong dir. Resolving each
            # to its real file in the data dirs is both correct and fast.
            plugin_paths, missing = self._resolve_plugin_paths()
            if missing:
                raise RuntimeError(
                    "Could not locate every requested plugin in the active data "
                    "directories: %s" % ", ".join(missing[:10])
                )

            self.progress.emit(
                self.tr("Sorting %d plugins with LOOT...") % len(plugin_paths)
            )
            sorted_active = self._sort_in_subprocess(plugin_paths, masterlist)

            self.result_sorted = sorted_active
            self.result_error = ""
            self.result_done = True
        except Exception as exc:  # any failure -> leave the order untouched
            self.result_sorted = None
            self.result_error = str(exc)
            self.result_done = True
        application = QCoreApplication.instance()
        if application is not None:
            # moveToThread must be called from the object's current thread.
            # Returning affinity to the GUI thread makes later Python/Qt object
            # destruction safe after the worker thread has been joined.
            self.moveToThread(application.thread())
        self.finished.emit(self.result_sorted, self.result_error)

    def _sort_in_subprocess(
        self, plugin_paths: list[str], masterlist: str | None
    ) -> list[str]:
        """Run the whole libloot pipeline in a clean child interpreter.

        See _LOOT_SUBPROCESS_SRC for why we never call libloot in MO's process.
        A hard timeout means a libloot hang can never freeze the UI: the child is
        killed and the load order is left untouched.
        """
        interpreter = self._find_interpreter()
        if interpreter is None:
            raise RuntimeError(
                "No compatible Python 3.12 interpreter was found to run LOOT "
                "out-of-process."
            )

        request = {
            "site_packages": _LOOT_SITE_PACKAGES,
            "game_path": self._game_path,
            "local_path": self._local_path,
            "data_dirs": list(self._data_dirs),
            "plugin_paths": list(plugin_paths),
            "active": list(self._active),
            "condition_active": list(self._condition_active),
            "masterlist": masterlist,
        }

        tmpdir = tempfile.mkdtemp(prefix="openmw_loot_")
        try:
            helper_path = os.path.join(tmpdir, "loot_sort_worker.py")
            req_path = os.path.join(tmpdir, "request.json")
            resp_path = os.path.join(tmpdir, "response.json")
            with open(helper_path, "w", encoding="utf-8") as fh:
                fh.write(_LOOT_SUBPROCESS_SRC)
            with open(req_path, "w", encoding="utf-8") as fh:
                json.dump(request, fh)

            qInfo(
                "OpenMW LOOT: launching child %r for %d plugins"
                % (interpreter, len(plugin_paths))
            )
            try:
                proc = subprocess.run(
                    [interpreter, helper_path, req_path, resp_path],
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    timeout=180,
                    check=False,
                )
            except subprocess.TimeoutExpired:
                raise RuntimeError(
                    "LOOT took too long and was stopped; the load order was left "
                    "unchanged."
                )

            if proc.returncode != 0:
                tail = (proc.stdout or b"").decode("utf-8", "replace")[-1500:]
                raise RuntimeError(
                    "The LOOT helper process failed (exit %d).\n%s"
                    % (proc.returncode, tail)
                )

            try:
                with open(resp_path, encoding="utf-8") as fh:
                    resp = json.load(fh)
            except Exception as exc:
                raise RuntimeError(f"Could not read the LOOT result ({exc}).")

            if resp.get("error"):
                raise RuntimeError(resp["error"])
            sorted_active = resp.get("sorted")
            if not sorted_active:
                raise RuntimeError("LOOT returned no sorted order.")
            qInfo("OpenMW LOOT: child returned %d sorted plugins" % len(sorted_active))
            return list(sorted_active)
        finally:
            try:
                import shutil

                shutil.rmtree(tmpdir, ignore_errors=True)
            except Exception:
                pass

    def _find_interpreter(self) -> str | None:
        """Locate a Python 3.12 able to import the bundled loot.so.

        loot.cpython-312 requires a 3.12 interpreter. We prefer one shipped next
        to the bundled runtime, then fall back to the system python3.12/python3.
        """
        import shutil

        candidates: list[str] = []
        if _LOOT_SITE_PACKAGES:
            # …/python/lib/python3.12/site-packages -> …/python/bin/python3.12
            runtime_root = os.path.dirname(
                os.path.dirname(os.path.dirname(_LOOT_SITE_PACKAGES))
            )
            candidates.append(os.path.join(runtime_root, "bin", "python3.12"))
        candidates += ["python3.12", "python3", "python"]

        for cand in candidates:
            exe = cand if os.path.isabs(cand) else shutil.which(cand)
            if exe and os.path.exists(exe) and self._interpreter_is_312(exe):
                return exe
        return None

    @staticmethod
    def _interpreter_is_312(exe: str) -> bool:
        try:
            out = subprocess.run(
                [exe, "-c", "import sys;print('%d.%d' % sys.version_info[:2])"],
                stdout=subprocess.PIPE,
                stderr=subprocess.DEVNULL,
                timeout=10,
                check=False,
            )
            return out.stdout.decode("utf-8", "replace").strip() == "3.12"
        except Exception:
            return False


class OpenMWSortWithLoot(mobase.IPluginTool, mobase.IPlugin):
    # NB: inherit BOTH mobase.IPluginTool and mobase.IPlugin and init both.
    # IPluginTool is bound with py::multiple_inheritance() and IPlugin as a base,
    # so a Python subclass that lists only IPluginTool raises a TypeError at
    # construction — which basic_games' createPlugins() loop silently swallows
    # (`except TypeError: pass`), leaving the tool unregistered with no log.
    def __init__(self) -> None:
        mobase.IPluginTool.__init__(self)
        mobase.IPlugin.__init__(self)
        self._organizer: mobase.IOrganizer | None = None
        # Transient per-run state for the threaded sort (see _run).
        self._sort_dialog: QProgressDialog | None = None
        self._sort_thread: QThread | None = None
        self._sort_worker: _LootWorker | None = None

    # ------------------------------------------------------------------
    # IPlugin
    # ------------------------------------------------------------------
    def init(self, organizer: mobase.IOrganizer) -> bool:
        self._organizer = organizer
        return True

    def name(self) -> str:
        return "OpenMW Sort With LOOT"

    def author(self) -> str:
        return "Fluorine OpenMW contributors"

    def description(self) -> str:
        return (
            "Sort the OpenMW Plugins tab with LOOT (libloot). Optional, native, "
            "and non-destructive: the load order is only changed if LOOT returns "
            "a valid result."
        )

    def version(self) -> mobase.VersionInfo:
        return mobase.VersionInfo(0, 1, 0)

    def settings(self) -> list[mobase.PluginSetting]:
        return [
            mobase.PluginSetting(
                "download_masterlist",
                "Download/refresh the LOOT masterlist before sorting",
                True,
            ),
            mobase.PluginSetting(
                "masterlist_url",
                "Advanced unpinned masterlist URL override",
                _DEFAULT_MASTERLIST_URL,
            ),
            mobase.PluginSetting(
                "openmw_install_path",
                "OpenMW install root or resources dir. Empty = explicit native/"
                "Flatpak auto-detection when unambiguous.",
                "",
            ),
        ]

    # ------------------------------------------------------------------
    # IPluginTool
    # ------------------------------------------------------------------
    def displayName(self) -> str:
        return "Sort with LOOT (OpenMW)"

    def tooltip(self) -> str:
        return "Sort the OpenMW Plugins tab using LOOT (libloot)."

    def icon(self):  # noqa: ANN201 - QIcon, default empty
        from PyQt6.QtGui import QIcon

        return QIcon()

    def display(self) -> None:
        try:
            self._run()
        except Exception as exc:  # never let the tool take down the UI
            qWarning(f"OpenMW LOOT: unexpected failure: {exc}")
            self._error(
                "LOOT sorting failed unexpectedly. The load order was not "
                f"changed.\n\n{exc}"
            )

    # ------------------------------------------------------------------
    # helpers
    # ------------------------------------------------------------------
    def _tr(self, text: str) -> str:
        return QCoreApplication.translate("OpenMWSortWithLoot", text)

    def _message(self, icon: QMessageBox.Icon, title: str, text: str) -> None:
        parent = None
        try:
            parent = self._parentWidget()
        except Exception:
            parent = None
        box = QMessageBox(
            icon,
            self._tr(title),
            self._tr(text),
            QMessageBox.StandardButton.Ok,
            parent,
        )
        box.exec()

    def _error(self, text: str) -> None:
        self._message(QMessageBox.Icon.Warning, "Sort with LOOT", text)

    def _info(self, text: str) -> None:
        self._message(QMessageBox.Icon.Information, "Sort with LOOT", text)

    def _confirm_sort(self) -> bool:
        parent = None
        try:
            parent = self._parentWidget()
        except Exception:
            pass
        answer = QMessageBox.question(
            parent,
            self._tr("Sort with LOOT"),
            self._tr(
                "LOOT metadata is not a substitute for list-specific compatibility "
                "patches. Curated Wabbajack orders may depend on their exact order. "
                "Sort only a copied/test profile unless the list author supports "
                "LOOT sorting.\n\nContinue?"
            ),
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
            QMessageBox.StandardButton.No,
        )
        return answer == QMessageBox.StandardButton.Yes

    def _setting(self, key: str, default):  # noqa: ANN001
        assert self._organizer is not None
        try:
            value = self._organizer.pluginSetting(self.name(), key)
        except Exception:
            return default
        return default if value is None else value

    # --- inputs from the game / mod list -------------------------------

    def _game_plugins_feature(self):  # noqa: ANN201
        assert self._organizer is not None
        try:
            return self._organizer.gameFeatures().gameFeature(mobase.GamePlugins)
        except Exception as exc:
            raise RuntimeError(
                f"Could not access the OpenMW GamePlugins feature ({exc})."
            )

    def _loot_snapshot(
        self, plugin_list: mobase.IPluginList
    ) -> tuple[object, object, tuple[PluginSlot, ...]]:
        """Read the state-backed adapter's complete immutable sort snapshot.

        Required adapter contract:
        ``lootSortSnapshot(plugin_list) -> {revision, rows}``, where rows are in
        complete canonical state order and include name/active/available/primary/
        groundcover. ``applyLootOrder`` below owns UI mutation, three-file
        persistence, exact verification, and rollback.
        """
        feature = self._game_plugins_feature()
        snapshotter = getattr(feature, "lootSortSnapshot", None)
        applier = getattr(feature, "applyLootOrder", None)
        if not callable(snapshotter) or not callable(applier):
            raise RuntimeError(
                "OpenMW GamePlugins does not yet provide the state-backed LOOT "
                "transaction API (lootSortSnapshot/applyLootOrder). No order was "
                "changed."
            )
        snapshot = snapshotter(plugin_list)
        if not isinstance(snapshot, Mapping) or "revision" not in snapshot:
            raise RuntimeError("GamePlugins returned an invalid LOOT snapshot.")
        raw_rows = snapshot.get("rows")
        if not isinstance(raw_rows, Sequence):
            raise RuntimeError("GamePlugins returned no canonical LOOT rows.")

        rows: list[PluginSlot] = []
        for raw in raw_rows:
            if isinstance(raw, PluginSlot):
                slot = raw
            elif isinstance(raw, Mapping):
                try:
                    slot = PluginSlot(
                        name=str(raw["name"]),
                        active=bool(raw["active"]),
                        available=bool(raw["available"]),
                        primary=bool(raw["primary"]),
                        groundcover=bool(raw["groundcover"]),
                    )
                except (KeyError, TypeError) as exc:
                    raise RuntimeError(
                        f"GamePlugins returned an invalid LOOT row ({exc})."
                    )
            else:
                raise RuntimeError("GamePlugins returned an invalid LOOT row type.")
            if slot.movable:
                try:
                    masters = tuple(
                        str(name) for name in plugin_list.masters(slot.name)
                    )
                except Exception as exc:
                    raise RuntimeError(
                        f"Could not read required masters for '{slot.name}' ({exc})."
                    )
                slot = replace(slot, required_masters=masters)
            rows.append(slot)

        _casefold_index((slot.name for slot in rows), "The canonical plugin state")
        validate_required_masters(rows)
        if not any(slot.movable for slot in rows):
            raise ValueError("There are no movable active plugins to sort with LOOT.")
        loot_sort_request(rows)
        return feature, snapshot["revision"], tuple(rows)

    def _data_directories(self, game) -> list[Path]:  # noqa: ANN001
        """Every directory LOOT must search to resolve a plugin filename.

        Mirrors the data= ordering game_openmw.py writes to openmw.cfg: vanilla
        Data Files first (lowest priority), then each active mod in profile
        order, then Overwrite last. libloot searches OpenMW data dirs in reverse,
        so the last entry wins — matching OpenMW's own precedence.
        """
        assert self._organizer is not None
        dirs: list[Path] = []
        try:
            data_files = Path(game.gameDirectory().absolutePath()) / "Data Files"
            if data_files.is_dir():
                dirs.append(data_files)
        except Exception:
            pass

        modlist = self._organizer.modList()
        try:
            ordered = modlist.allModsByProfilePriority()
        except Exception:
            ordered = []
        for mod_name in ordered:
            if mod_name == "Overwrite":
                continue
            try:
                if not (modlist.state(mod_name) & mobase.ModState.ACTIVE):
                    continue
                mod = modlist.getMod(mod_name)
            except Exception:
                continue
            if mod is None:
                continue
            mod_path = Path(mod.absolutePath())
            if mod_path.is_dir():
                dirs.append(mod_path)

        try:
            overwrite = modlist.getMod("Overwrite")
            if overwrite is not None:
                ov_path = Path(overwrite.absolutePath())
                if ov_path.is_dir() and any(ov_path.iterdir()):
                    dirs.append(ov_path)
        except Exception:
            pass
        return dirs

    @staticmethod
    def _resource_from_cfg(
        cfg: Path, token_roots: Mapping[str, Path] | None = None
    ) -> Path | None:
        if token_roots is None:
            token_roots = {token: cfg.parent for token in _OPENMW_PATH_TOKENS}
        try:
            return _effective_resource_from_cfg(cfg, token_roots)
        except (OSError, ValueError):
            return None

    def _resource_candidates(self) -> tuple[list[Path], list[Path]]:
        """Discover native and Flatpak resources without choosing launch config."""
        native: list[Path] = []
        flatpak: list[Path] = []
        executable_contexts: dict[tuple[Path, Path], tuple[Path, Path]] = {}

        for executable_name in ("openmw", "openmw-launcher"):
            executable = shutil.which(executable_name)
            if not executable:
                continue
            resolved = Path(executable).resolve()
            prefix = resolved.parent.parent
            executable_contexts.setdefault(
                (resolved.parent, prefix), (resolved.parent, prefix)
            )
            native.extend(
                (
                    resolved.parent,
                    prefix,
                    prefix / "share/games/openmw",
                )
            )
        try:
            configured_executables = self._organizer.executablesList().executables()
            for configured in configured_executables:
                resolved = Path(
                    configured.binaryInfo().absoluteFilePath()
                ).resolve()
                if resolved.name.casefold() not in ("openmw", "openmw-launcher"):
                    continue
                prefix = resolved.parent.parent
                executable_contexts.setdefault(
                    (resolved.parent, prefix), (resolved.parent, prefix)
                )
                native.extend(
                    (
                        resolved.parent,
                        prefix,
                        prefix / "share/games/openmw",
                    )
                )
        except Exception:
            pass

        if not executable_contexts:
            executable_contexts[(Path("/usr/bin"), Path("/usr"))] = (
                Path("/usr/bin"),
                Path("/usr"),
            )
        for executable_dir, prefix in executable_contexts.values():
            roots = _native_token_roots(executable_dir, prefix)
            local_cfg = executable_dir / "openmw.cfg"
            package_cfg = prefix / "etc/openmw/openmw.cfg"
            system_cfg = Path("/etc/openmw/openmw.cfg")
            if local_cfg.is_file():
                cfg = local_cfg
            elif package_cfg.is_file():
                cfg = package_cfg
            elif system_cfg.is_file():
                cfg = system_cfg
            else:
                # This fallback is useful for portable/custom setups whose base
                # source is not host-visible, while still treating the user file
                # as one chain instead of collecting every resources= occurrence.
                cfg = roots["?userconfig?"] / "openmw.cfg"
            candidate = self._resource_from_cfg(cfg, roots)
            if candidate is not None:
                native.append(candidate)

        flatpak_cfg = (
            Path.home()
            / ".var/app/org.openmw.OpenMW/config/openmw/openmw.cfg"
        )
        flatpak_location: Path | None = None
        flatpak_executable = shutil.which("flatpak")
        if flatpak_executable:
            try:
                result = subprocess.run(
                    [
                        flatpak_executable,
                        "info",
                        "--show-location",
                        "org.openmw.OpenMW",
                    ],
                    stdout=subprocess.PIPE,
                    stderr=subprocess.DEVNULL,
                    timeout=10,
                    check=False,
                )
                if result.returncode == 0:
                    location = Path(
                        result.stdout.decode("utf-8", "replace").strip()
                    )
                    if location.is_absolute():
                        flatpak_location = location
                        flatpak.extend(
                            (
                                location / "files/share/games/openmw",
                                location / "share/games/openmw",
                            )
                        )
            except Exception as exc:
                qWarning(f"OpenMW LOOT: Flatpak resource discovery failed: {exc}")

        if flatpak_location is not None:
            flatpak_roots = _flatpak_token_roots(flatpak_location)
            flatpak_base_cfgs = (
                flatpak_location / "files/bin/openmw.cfg",
                flatpak_location / "files/etc/openmw/openmw.cfg",
            )
            flatpak_root_cfg = next(
                (cfg for cfg in flatpak_base_cfgs if cfg.is_file()), flatpak_cfg
            )
            candidate = self._resource_from_cfg(flatpak_root_cfg, flatpak_roots)
            if candidate is not None:
                flatpak.append(candidate)
        return native, flatpak

    def _resolve_game_path(self) -> ResourceSelection:
        native, flatpak = self._resource_candidates()
        return select_resource_root(
            str(self._setting("openmw_install_path", "")), native, flatpak
        )

    def _masterlist_plan(self) -> tuple[str | None, str, str | None, bool]:
        """Prepare the masterlist cache path/url on the main thread.

        The actual (blocking) download happens in the worker thread; here we only
        touch the organizer (main-thread only) to resolve the cache dir + settings.
        Returns (cache_path | None, url, expected_sha256 | None, want_download).
        """
        assert self._organizer is not None
        url = str(self._setting("masterlist_url", _DEFAULT_MASTERLIST_URL))
        want_download = bool(self._setting("download_masterlist", True))
        expected_sha256 = (
            _DEFAULT_MASTERLIST_SHA256 if url == _DEFAULT_MASTERLIST_URL else None
        )
        if expected_sha256 is None:
            qWarning("OpenMW LOOT: using an unpinned advanced masterlist URL")
        try:
            cache_dir = Path(self._organizer.pluginDataPath()) / "openmw_loot"
            cache_dir.mkdir(parents=True, exist_ok=True)
            if expected_sha256 is not None:
                cache_name = f"masterlist-{_MASTERLIST_COMMIT[:12]}.yaml"
            else:
                url_key = hashlib.sha256(url.encode("utf-8")).hexdigest()[:12]
                cache_name = f"masterlist-unpinned-{url_key}.yaml"
            return (
                str(cache_dir / cache_name),
                url,
                expected_sha256,
                want_download,
            )
        except Exception as exc:
            qWarning(f"OpenMW LOOT: cannot prepare masterlist cache: {exc}")
            return None, url, expected_sha256, want_download

    # --- apply ---------------------------------------------------------

    def _apply(
        self,
        plugin_list: mobase.IPluginList,
        feature: object,
        revision: object,
        slots: Sequence[PluginSlot],
        sorted_request: Sequence[str],
        primary_order: Sequence[str],
    ) -> int:
        """Delegate mutation/persistence/rollback to the state-backed adapter."""
        merge = merge_active_slots(slots, sorted_request, primary_order)
        applier = getattr(feature, "applyLootOrder", None)
        if not callable(applier):
            raise RuntimeError("GamePlugins lost its state-backed LOOT apply API.")
        result = applier(plugin_list, revision, list(merge.order))
        if result is False:
            raise RuntimeError("GamePlugins rejected the transactional LOOT order.")
        assert self._organizer is not None
        try:
            self._organizer.refresh()
        except Exception as exc:
            qWarning(
                "OpenMW LOOT: order persisted successfully, but refresh failed: "
                f"{exc}"
            )
        return merge.moved

    # --- main ----------------------------------------------------------

    def _run(self) -> None:
        assert self._organizer is not None
        if not _LOOT_AVAILABLE:
            self._error(
                "LOOT support (libloot) is not available in this build, so "
                "sorting cannot run. The Plugins tab is unchanged.\n\n"
                f"Import error: {_LOOT_IMPORT_ERROR}"
            )
            return

        game = self._organizer.managedGame()
        try:
            from ...game_openmw import OpenMWGame

            if not isinstance(game, OpenMWGame):
                self._error("This tool only works with the OpenMW game plugin.")
                return
        except Exception:
            # If we cannot even import the game class, bail safely.
            self._error("Could not resolve the OpenMW game plugin.")
            return

        plugin_list = self._organizer.pluginList()
        try:
            feature, revision, slots = self._loot_snapshot(plugin_list)
        except Exception as exc:
            self._error(str(exc))
            return
        request = loot_sort_request(slots)
        primary_order = tuple(str(name) for name in game.primaryPlugins())
        if not self._confirm_sort():
            return

        try:
            resources = self._resolve_game_path()
        except Exception as exc:
            self._error(str(exc))
            return
        data_dirs = self._data_directories(game)
        ml_cache, ml_url, ml_sha256, ml_download = self._masterlist_plan()
        qInfo(
            "OpenMW LOOT: inputs gathered; resource_root=%r installation=%s "
            "data_dirs=%d request=%d ml_cache=%r"
            % (
                str(resources.root),
                resources.installation,
                len(data_dirs),
                len(request),
                ml_cache,
            )
        )

        # --- run the blocking libloot pipeline off the UI thread -------
        # All inputs are plain data; the worker never touches a mobase/Qt object.
        # The result is applied back here, on the main thread, after the dialog
        # closes. On any failure the load order is left exactly as it was.
        worker = _LootWorker(
            str(resources.root),
            None,
            [str(p) for p in data_dirs],
            list(request),
            [
                slot.name
                for slot in slots
                if slot.active and slot.available and not slot.groundcover
            ],
            ml_cache,
            ml_url,
            ml_sha256,
            ml_download,
        )
        # No parent: `self` is a mobase plugin (IPluginTool/IPlugin), NOT a
        # QObject, so it can't parent a QThread. We keep a strong ref in
        # self._sort_thread below to stop it being GC'd while it runs.
        thread = QThread()
        worker.moveToThread(thread)

        parent = None
        try:
            parent = self._parentWidget()
        except Exception:
            parent = None

        dialog = _LootProgressDialog(
            self._tr("Preparing to sort with LOOT..."), "", 0, 0, parent
        )
        dialog.setWindowTitle(self._tr("Sort with LOOT (OpenMW)"))
        dialog.setWindowModality(Qt.WindowModality.WindowModal)
        dialog.setCancelButton(None)  # libloot can't be safely interrupted
        dialog.setMinimumDuration(0)
        dialog.setAutoClose(False)
        dialog.setAutoReset(False)
        dialog.setMinimumWidth(380)

        self._sort_dialog = dialog
        self._sort_thread = thread
        self._sort_worker = worker

        # Route the worker's signals to QObjects that live on the main thread
        # (the dialog and the thread). `self` is a mobase plugin, not a QObject,
        # so it can't be a queued-connection receiver — connecting to its bound
        # methods would run the slots on the worker thread and touch the GUI off
        # the UI thread. The worker stashes its result on itself; we read it
        # after thread.wait() (a join, so the read is safely ordered).
        worker.progress.connect(
            dialog.setLabelText, Qt.ConnectionType.QueuedConnection
        )
        # Stop the thread's event loop and close the modal dialog (which makes
        # exec() return) once the worker is done.
        # quit() is thread-safe. A direct connection is required so an early
        # title-bar close followed by wait() cannot starve a queued quit call on
        # the blocked GUI thread.
        worker.finished.connect(thread.quit, Qt.ConnectionType.DirectConnection)
        worker.finished.connect(
            dialog.finishWorker, Qt.ConnectionType.QueuedConnection
        )
        thread.started.connect(worker.run)

        try:
            thread.start()
            dialog.exec()  # spins the event loop until the worker is done
            # The title-bar close button can end dialog.exec() even though the
            # worker is still inside libloot. Never release a live QThread; the
            # subprocess has its own bounded timeout and will eventually finish.
            thread.wait()
        finally:
            self._sort_dialog = None
            self._sort_thread = None

        done = worker.result_done
        error = worker.result_error
        sorted_active = worker.result_sorted
        self._sort_worker = None

        if not done:
            self._error(
                "LOOT sorting did not complete, so the load order was not changed."
            )
            return
        if error:
            self._error(
                "LOOT could not sort the plugins, so the load order was left "
                f"unchanged.\n\n{error}"
            )
            return

        if not sorted_active:
            self._error(
                "LOOT returned no result, so the load order was not changed."
            )
            return

        try:
            moved = self._apply(
                plugin_list,
                feature,
                revision,
                slots,
                list(sorted_active),
                primary_order,
            )
        except Exception as exc:
            import traceback

            qWarning(
                "OpenMW LOOT: _apply failed: %s\n%s"
                % (exc, traceback.format_exc())
            )
            self._error(
                "The sorted order from LOOT could not be applied safely, so the "
                f"load order was left unchanged.\n\n{exc}"
            )
            return

        if moved == 0:
            self._info("Nothing was moved.")
        else:
            self._info(f"{moved} plugin(s) were moved.")

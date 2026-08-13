"""
openmw_cfg.py — generate the managed block of an ``openmw.cfg``.

OpenMW does not use a separate VFS injector: it reads its mods directly from an
ordered list of ``data=`` directories and loads plugins in the order of the
``content=`` lines.  This module rewrites *only* the keys we own, leaving every
other line (engine settings, comments, unrelated keys) untouched:

    data=               asset/plugin search dirs; LATER entries override earlier
    content=            ordered plugin load list (.esp/.esm/.omwaddon/.omwscripts)
    groundcover=        grass/groundcover plugins (kept out of content= for perf)
    fallback-archive=   .bsa archives; later entries override earlier

It is deliberately pure standard library (no Qt / mobase) so it can be unit
tested on its own and reused by the OpenMW game plugin.  The caller decides what
goes into ``data_dirs`` — a single merged dir (Fluorine FUSE) or one entry per
mod (native OpenMW VFS) — this module stays agnostic about that choice.
"""

from __future__ import annotations

import base64
import binascii
import json
import os
import re
import shutil
import sys
import tempfile
from collections import deque
from contextlib import contextmanager, suppress
from pathlib import Path
from typing import Iterable, Iterator, Mapping, TextIO, TypedDict

# Morrowind masters, in canonical load order.
#
# Do NOT list ``builtin.omwscripts`` here.  It is OpenMW's built-in Lua bundle,
# shipped inside the engine's ``resources/vfs-mw`` and loaded *implicitly* by the
# engine itself.  If we also emit ``content=builtin.omwscripts`` it ends up
# specified twice across the config chain and OpenMW aborts on startup with
# "Content file specified more than once: builtin.omwscripts. Aborting...".
VANILLA_MASTERS: list[str] = [
    "Morrowind.esm",
    "Tribunal.esm",
    "Bloodmoon.esm",
]

# Vanilla BSAs, always emitted as the lowest-priority fallback-archive entries.
VANILLA_BSAS: list[str] = [
    "Morrowind.bsa",
    "Tribunal.bsa",
    "Bloodmoon.bsa",
]

# Keys this module fully owns (lowercase, exact match).
_MANAGED_KEYS = frozenset({"data", "content", "groundcover", "fallback-archive"})
# The global OpenMW config contributes its required resources/vfs-mw data dir.
# Keep inherited data entries while replacing the other generated lists.
_REPLACED_MANAGED_KEYS = _MANAGED_KEYS - {"data"}

_PROFILE_BEGIN = "# BEGIN FLUORINE OPENMW PROFILE"
_PROFILE_END = "# END FLUORINE OPENMW PROFILE"
_LOCAL_SAVES_BEGIN = "# BEGIN FLUORINE OPENMW LOCAL SAVES"
_LOCAL_SAVES_END = "# END FLUORINE OPENMW LOCAL SAVES"
_LOCAL_SAVES_ORIGINAL = "# FLUORINE ORIGINAL USER-DATA "

_SELECTION_KEYS = ("content", "groundcover", "fallback-archive")
_OPENMW_PATH_TOKENS = frozenset(
    {"?local?", "?userconfig?", "?userdata?", "?global?"}
)
_SELECTION_STATE_VERSION = 3
_OPENMW_NATIVE_SUFFIXES = (".omwaddon", ".omwgame", ".omwscripts")
_OPENMW_PLAYER_STUB_SUFFIXES = tuple(
    suffix + ".esp" for suffix in _OPENMW_NATIVE_SUFFIXES
)


class OpenMWSelectionStateV1(TypedDict):
    version: int
    known_plugins: list[str]
    enabled_plugins: list[str]
    groundcover: list[str]
    known_archives: list[str]
    archives: list[str]


class OpenMWSelectionStateV2(OpenMWSelectionStateV1):
    profile_config_entries: list[str]
    profile_config_entries_known: bool
    profile_config_terminal: bool


class OpenMWSelectionState(TypedDict):
    version: int
    plugin_order: list[str]
    enabled_plugins: list[str]
    groundcover: list[str]
    known_archives: list[str]
    archives: list[str]
    profile_config_entries: list[str]
    profile_config_entries_known: bool
    profile_config_terminal: bool
    content_migration_source: str
    groundcover_migration_source: str
    archive_migration_source: str
    order_migration_source: str
    plugin_state_migrated: bool


class OpenMWConfigSelection(TypedDict):
    content: list[str]
    groundcover: list[str]
    fallback_archive: list[str]
    content_present: bool
    groundcover_present: bool
    fallback_archive_present: bool
    plugin_order: list[str]
    replaced_channels: list[str]


class _OpenMWConfigSource(TypedDict):
    selection: OpenMWConfigSelection
    plugin_entries: list[tuple[str, str]]
    config_dirs: list[str]
    replace_config: bool


class MorrowindIniSelection(TypedDict):
    game_files: list[str]
    archives: list[str]
    game_files_present: bool
    archives_present: bool


class PluginInventoryReconciliation(TypedDict):
    state: OpenMWSelectionState
    available_plugins: list[str]
    enabled_plugins: list[str]
    active_plugins: list[str]
    unavailable_plugins: list[str]
    newly_discovered: list[str]
    insertion_positions: dict[str, int]
    alias_diagnostics: list[str]
    ignored_alias_diagnostics: list[str]
    ignored_alias_identities: list[str]
    ignored_alias_counts: dict[str, int]
    wrapper_only_alias_diagnostics: list[str]
    wrapper_only_alias_identities: list[str]
    wrapper_only_alias_counts: dict[str, int]
    duplicate_diagnostics: list[str]


class OpenMWLauncherProfile(TypedDict):
    current_profile: str | None
    data: list[str]
    content: list[str]
    fallback_archive: list[str]


class OpenMWLauncherInspection(TypedDict):
    profile: OpenMWLauncherProfile
    general_initialized: bool


def find_openmw_cfg(
    native_cfg: Path, flatpak_cfg: Path, flatpak_launch: bool
) -> Path | None:
    candidate = flatpak_cfg if flatpak_launch else native_cfg
    return candidate if candidate.is_file() else None


def escape_data_path(path: str) -> str:
    """Quote/escape a path for a ``data=`` line.

    openmw.cfg uses boost::filesystem quoting: ``&`` and ``"`` are escaped with a
    leading ``&`` and the whole value is wrapped in double quotes.  This matches
    AnyOldName3's MO2 exporter so paths containing spaces or quotes round-trip.
    """
    out = ['"']
    for ch in path:
        if ch in ("&", '"'):
            out.append("&")
        out.append(ch)
    out.append('"')
    return "".join(out)


def _is_managed(line: str) -> bool:
    s = line.strip()
    if not s or s.startswith("#") or "=" not in s:
        return False
    return s.split("=", 1)[0].strip().lower() in _MANAGED_KEYS


def _read_lines(cfg_path: Path) -> list[str]:
    if not cfg_path.is_file():
        return []
    return cfg_path.read_text(encoding="utf-8", errors="replace").splitlines()


def _parse_openmw_config_path(value: str, cfg_path: Path) -> str:
    value = value.strip()
    if not value:
        raise ValueError(f"Malformed OpenMW config directive in {cfg_path}")
    if not value.startswith('"'):
        return value

    result: list[str] = []
    escaped = False
    closed = False
    position = 1
    while position < len(value):
        character = value[position]
        position += 1
        if escaped:
            result.append(character)
            escaped = False
        elif character == "&":
            escaped = True
        elif character == '"':
            closed = True
            break
        else:
            result.append(character)
    if escaped or not closed or not result:
        raise ValueError(f"Malformed OpenMW config path in {cfg_path}: {value!r}")
    return "".join(result)


def _default_openmw_path_tokens(root_cfg: Path) -> dict[str, Path]:
    """Return platform roots that can be inferred without the OpenMW binary."""
    tokens: dict[str, Path] = {}
    if not sys.platform.startswith("linux"):
        return tokens

    config_dir = root_cfg.parent
    app_root: Path | None = None
    if (
        config_dir.name == "openmw"
        and config_dir.parent.name == "config"
        and config_dir.parent.parent.parent.name == "app"
        and config_dir.parent.parent.parent.parent.name == ".var"
    ):
        app_root = config_dir.parent.parent

    if app_root is not None:
        tokens["?userconfig?"] = config_dir
        tokens["?userdata?"] = app_root / "data" / "openmw"
    else:
        home_env = os.environ.get("HOME")
        home = Path(home_env) if home_env else Path.home()
        config_home = Path(os.environ.get("XDG_CONFIG_HOME", home / ".config"))
        data_home = Path(os.environ.get("XDG_DATA_HOME", home / ".local/share"))
        tokens["?userconfig?"] = config_home / "openmw"
        tokens["?userdata?"] = data_home / "openmw"
    tokens["?global?"] = Path("/usr/share/games/openmw")
    return tokens


def _openmw_path_tokens(
    root_cfg: Path,
    path_tokens: Mapping[str, Path | str] | None,
) -> dict[str, Path]:
    tokens = (
        _default_openmw_path_tokens(root_cfg)
        if path_tokens is None else {}
    )
    for raw_token, raw_path in (path_tokens or {}).items():
        token = raw_token
        if token not in _OPENMW_PATH_TOKENS:
            raise ValueError(f"Unknown OpenMW path token: {raw_token!r}")
        if not str(raw_path):
            raise ValueError(f"Empty OpenMW path token root: {raw_token!r}")
        path = Path(raw_path).expanduser()
        if not path.is_absolute():
            raise ValueError(
                f"OpenMW path token root must be absolute: {raw_token!r}"
            )
        tokens[token] = path.resolve(strict=False)
    return tokens


def _resolve_openmw_config_dir(
    value: str,
    cfg_path: Path,
    path_tokens: Mapping[str, Path],
) -> Path:
    token_match = re.match(r"^(\?[^?]+\?)(.*)$", value)
    if token_match is not None:
        token = token_match.group(1)
        root = path_tokens.get(token)
        if token not in _OPENMW_PATH_TOKENS or root is None:
            raise ValueError(
                f"Unresolved OpenMW path token in {cfg_path}: "
                f"{token_match.group(1)!r}"
            )
        remainder = token_match.group(2).lstrip("/\\")
        directory = root / remainder if remainder else root
    elif value.startswith("?"):
        raise ValueError(
            f"Unresolved OpenMW path token in {cfg_path}: {value!r}"
        )
    else:
        directory = Path(value).expanduser()
        if not directory.is_absolute():
            directory = cfg_path.parent / directory
    return directory.resolve(strict=False)


def _parse_openmw_source(
    cfg_path: Path, *, parse_config_dirs: bool
) -> _OpenMWConfigSource:
    values = {key: [] for key in _SELECTION_KEYS}
    seen = {key: set() for key in _SELECTION_KEYS}
    present = {key: False for key in _SELECTION_KEYS}
    replaced: list[str] = []
    plugin_entries: list[tuple[str, str]] = []
    config_dirs: list[str] = []
    replace_config = False

    for raw in _read_lines(cfg_path):
        line = raw.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        raw_key, raw_value = line.split("=", 1)
        key = raw_key.strip().casefold()
        value = raw_value.strip()
        if key == "replace":
            targets = value.split()
            if not targets or any(
                re.fullmatch(r"[A-Za-z0-9-]+", target) is None
                for target in targets
            ):
                raise ValueError(f"Malformed OpenMW replace directive: {raw!r}")
            for raw_target in targets:
                target = raw_target.casefold()
                if target == "config":
                    replace_config = True
                    continue
                if target not in values:
                    continue
                present[target] = True
                if target not in replaced:
                    replaced.append(target)
            continue
        if key == "config":
            if parse_config_dirs:
                config_dirs.append(_parse_openmw_config_path(value, cfg_path))
            continue
        if key not in values or not value:
            continue
        if key in ("content", "groundcover"):
            value = canonical_plugin_name(value)
        present[key] = True
        folded = value.casefold()
        if folded in seen[key]:
            continue
        seen[key].add(folded)
        values[key].append(value)
        if key in ("content", "groundcover"):
            plugin_entries.append((key, value))

    return {
        "selection": {
            "content": values["content"],
            "groundcover": values["groundcover"],
            "fallback_archive": values["fallback-archive"],
            "content_present": present["content"],
            "groundcover_present": present["groundcover"],
            "fallback_archive_present": present["fallback-archive"],
            "plugin_order": [value for _, value in plugin_entries],
            "replaced_channels": replaced,
        },
        "plugin_entries": plugin_entries,
        "config_dirs": config_dirs,
        "replace_config": replace_config,
    }


def parse_openmw_selection(cfg_path: Path) -> OpenMWConfigSelection:
    """Parse one config source without applying its replacement directives."""
    return _parse_openmw_source(cfg_path, parse_config_dirs=False)["selection"]


def parse_openmw_selection_chain(
    cfg_path: Path,
    path_tokens: Mapping[str, Path | str] | None = None,
) -> OpenMWConfigSelection:
    """Parse the effective selection from an OpenMW ``config=`` chain.

    Config sources are visited breadth-first in priority order. Replacement
    directives apply to lower-priority sources as a whole, regardless of where
    the directive occurs within their own file. ``path_tokens`` can provide
    exact roots for OpenMW's platform-dependent path tokens.
    """
    root_cfg = cfg_path.expanduser().resolve(strict=False)
    tokens = _openmw_path_tokens(root_cfg, path_tokens)
    sources: list[tuple[Path, _OpenMWConfigSource]] = []
    pending: deque[tuple[Path, frozenset[Path]]] = deque(
        [(root_cfg, frozenset())]
    )
    discovered = {root_cfg}

    while pending:
        path, ancestors = pending.popleft()
        if not path.is_file():
            continue

        source = _parse_openmw_source(path, parse_config_dirs=True)
        if source["replace_config"] and path != root_cfg:
            sources = sources[:1]
            pending.clear()
            discovered = {root_cfg, path}
        sources.append((path, source))

        next_ancestors = ancestors | {path}
        for raw_directory in source["config_dirs"]:
            directory = _resolve_openmw_config_dir(
                raw_directory, path, tokens
            )
            child = (directory / "openmw.cfg").resolve(strict=False)
            if child in next_ancestors:
                raise ValueError(f"Cycle in OpenMW config chain at {child}")
            if child in discovered:
                raise ValueError(
                    f"Ambiguous repeated OpenMW config source: {child}"
                )
            discovered.add(child)
            pending.append((child, frozenset(next_ancestors)))

    values = {key: [] for key in _SELECTION_KEYS}
    seen = {key: set() for key in _SELECTION_KEYS}
    present = {key: False for key in _SELECTION_KEYS}
    replaced: list[str] = []
    plugin_entries: list[tuple[str, str]] = []
    for _, source in sources:
        selection = source["selection"]
        for key in selection["replaced_channels"]:
            values[key].clear()
            seen[key].clear()
            plugin_entries = [
                entry for entry in plugin_entries if entry[0] != key
            ]
            present[key] = True
            if key not in replaced:
                replaced.append(key)

        added = {key: set() for key in _SELECTION_KEYS}
        fields = (
            ("content", selection["content"], selection["content_present"]),
            (
                "groundcover",
                selection["groundcover"],
                selection["groundcover_present"],
            ),
            (
                "fallback-archive",
                selection["fallback_archive"],
                selection["fallback_archive_present"],
            ),
        )
        for key, source_values, source_present in fields:
            present[key] = present[key] or source_present
            for value in source_values:
                folded = value.casefold()
                if folded in seen[key]:
                    continue
                seen[key].add(folded)
                added[key].add(folded)
                values[key].append(value)
        for key, value in source["plugin_entries"]:
            if value.casefold() in added[key]:
                plugin_entries.append((key, value))

    return {
        "content": values["content"],
        "groundcover": values["groundcover"],
        "fallback_archive": values["fallback-archive"],
        "content_present": present["content"],
        "groundcover_present": present["groundcover"],
        "fallback_archive_present": present["fallback-archive"],
        "plugin_order": [value for _, value in plugin_entries],
        "replaced_channels": replaced,
    }


def read_openmw_selection(cfg_path: Path) -> dict[str, list[str]]:
    """Compatibility view of the effective OpenMW config chain selection."""
    parsed = parse_openmw_selection_chain(cfg_path)
    return {
        "content": parsed["content"],
        "groundcover": parsed["groundcover"],
        "fallback-archive": parsed["fallback_archive"],
    }


def filter_selected_files(
    available: Iterable[str], selected: Iterable[str]
) -> list[str]:
    """Keep available files selected by name, preserving available-file order."""
    selected_keys = {name.casefold() for name in selected}
    return [name for name in available if name.casefold() in selected_keys]


def order_selected_files(
    available: Iterable[str], selected: Iterable[str]
) -> list[str]:
    """Return selected available files in selection order and provider casing."""
    available_by_name: dict[str, str] = {}
    for name in available:
        available_by_name[name.casefold()] = name

    result: list[str] = []
    emitted: set[str] = set()
    for name in selected:
        folded = name.casefold()
        if folded in available_by_name and folded not in emitted:
            emitted.add(folded)
            result.append(available_by_name[folded])
    return result


def _unique_names(*groups: Iterable[str]) -> list[str]:
    result: list[str] = []
    seen: set[str] = set()
    for group in groups:
        for name in group:
            folded = name.casefold()
            if name and folded not in seen:
                seen.add(folded)
                result.append(name)
    return result


def collapse_file_providers(available: Iterable[str]) -> list[str]:
    """Retain the highest-priority usable provider for each logical identity."""
    native_order, providers, _, _, _, _, _, _, _, _ = _inventory_providers(
        available
    )
    return [providers[key] for key in native_order]


def is_openmw_player_stub(name: str) -> bool:
    return name.casefold().endswith(_OPENMW_PLAYER_STUB_SUFFIXES)


def destub_plugin_name(name: str) -> str:
    return name[:-4] if is_openmw_player_stub(name) else name


def canonical_plugin_name(name: str) -> str:
    """Return the canonical logical identity for a native file or wrapper."""
    return destub_plugin_name(name)


def normalize_plugin_loadorder(names: Iterable[str]) -> list[str]:
    result: list[str] = []
    seen: set[str] = set()
    for raw in names:
        name = canonical_plugin_name(raw)
        folded = name.casefold()
        if folded not in seen:
            seen.add(folded)
            result.append(name)
    return result


def order_plugins_by_loadorder(
    available: Iterable[str], loadorder: Iterable[str]
) -> list[str]:
    available = collapse_file_providers(available)
    normalized = normalize_plugin_loadorder(loadorder)
    if not normalized:
        return available
    rank = {name.casefold(): index for index, name in enumerate(normalized)}
    return sorted(
        available,
        key=lambda name: (
            (0, rank[name.casefold()])
            if name.casefold() in rank
            else (1, 0)
        ),
    )


def unranked_native_plugins(
    content_plugins: Iterable[str], loadorder: Iterable[str]
) -> list[str]:
    normalized = normalize_plugin_loadorder(loadorder)
    if not normalized:
        return []
    ranked = {name.casefold() for name in normalized}
    result: list[str] = []
    seen: set[str] = set()
    for name in content_plugins:
        folded = name.casefold()
        if (
            folded.endswith(_OPENMW_NATIVE_SUFFIXES)
            and folded not in ranked
            and folded not in seen
        ):
            seen.add(folded)
            result.append(name)
    return result


def format_name_sample(names: Iterable[str], limit: int = 10) -> str:
    names = list(names)
    shown = ", ".join(repr(name) for name in names[:limit])
    omitted = len(names) - limit
    return f"{shown} (+{omitted} more)" if omitted > 0 else shown


def _canonical_plugin_names(*groups: Iterable[str]) -> list[str]:
    return normalize_plugin_loadorder(
        name.strip() for group in groups for name in group if name.strip()
    )


def _copy_v3_state(state: OpenMWSelectionState) -> OpenMWSelectionState:
    return {
        "version": _SELECTION_STATE_VERSION,
        "plugin_order": list(state["plugin_order"]),
        "enabled_plugins": list(state["enabled_plugins"]),
        "groundcover": list(state["groundcover"]),
        "known_archives": list(state["known_archives"]),
        "archives": list(state["archives"]),
        "profile_config_entries": list(state["profile_config_entries"]),
        "profile_config_entries_known": state["profile_config_entries_known"],
        "profile_config_terminal": state["profile_config_terminal"],
        "content_migration_source": state["content_migration_source"],
        "groundcover_migration_source": state["groundcover_migration_source"],
        "archive_migration_source": state["archive_migration_source"],
        "order_migration_source": state["order_migration_source"],
        "plugin_state_migrated": state["plugin_state_migrated"],
    }


def _validate_string_list(data: dict, key: str, *, nonempty: bool = False) -> bool:
    value = data.get(key)
    return isinstance(value, list) and all(
        isinstance(item, str) and (not nonempty or bool(item)) for item in value
    )


def _validate_legacy_selection_state(data: object) -> dict:
    if not isinstance(data, dict) or type(data.get("version")) is not int:
        raise ValueError("Invalid OpenMW selection state")
    version = data["version"]
    if version not in (1, 2):
        raise ValueError("Unsupported OpenMW selection state")
    for key in (
        "known_plugins",
        "enabled_plugins",
        "groundcover",
        "known_archives",
        "archives",
    ):
        if not _validate_string_list(data, key):
            raise ValueError("Invalid OpenMW selection state")
    if version == 2 and (
        not _validate_string_list(data, "profile_config_entries")
        or not isinstance(data.get("profile_config_entries_known"), bool)
        or not isinstance(data.get("profile_config_terminal"), bool)
    ):
        raise ValueError("Invalid OpenMW selection state")
    return data


def validate_selection_state(state: object) -> OpenMWSelectionState:
    """Strictly validate canonical serialized version-3 state."""
    if (
        not isinstance(state, dict)
        or state.get("version") != _SELECTION_STATE_VERSION
    ):
        raise ValueError("Invalid OpenMW version-3 selection state")
    if "known_plugins" in state:
        raise ValueError("Version-3 state must not contain known_plugins")
    for key in (
        "plugin_order",
        "enabled_plugins",
        "groundcover",
        "known_archives",
        "archives",
        "profile_config_entries",
    ):
        if not _validate_string_list(
            state, key, nonempty=key != "profile_config_entries"
        ):
            raise ValueError(f"Invalid OpenMW version-3 field: {key}")
    for key in (
        "profile_config_entries_known",
        "profile_config_terminal",
        "plugin_state_migrated",
    ):
        if not isinstance(state.get(key), bool):
            raise ValueError(f"Invalid OpenMW version-3 field: {key}")
    for key in (
        "content_migration_source",
        "groundcover_migration_source",
        "archive_migration_source",
        "order_migration_source",
    ):
        if not isinstance(state.get(key), str) or not state[key]:
            raise ValueError(f"Invalid OpenMW version-3 field: {key}")

    order_keys: set[str] = set()
    for name in state["plugin_order"]:
        if name != name.strip():
            raise ValueError("Version-3 plugin_order contains a noncanonical name")
        if is_openmw_player_stub(name):
            raise ValueError("Version-3 plugin_order contains a wrapper alias")
        folded = name.casefold()
        if folded in order_keys:
            raise ValueError("Version-3 plugin_order contains duplicate identities")
        order_keys.add(folded)
    for field in ("enabled_plugins", "groundcover"):
        seen: set[str] = set()
        for name in state[field]:
            if name != name.strip():
                raise ValueError(f"Version-3 {field} contains a noncanonical name")
            if is_openmw_player_stub(name):
                raise ValueError(f"Version-3 {field} contains a wrapper alias")
            folded = name.casefold()
            if folded in seen:
                raise ValueError(f"Version-3 {field} contains duplicate identities")
            if folded not in order_keys:
                raise ValueError(f"Version-3 {field} contains an unknown identity")
            seen.add(folded)
    return state


def _inventory_providers(
    rows: Iterable[str],
) -> tuple[
    list[str],
    dict[str, str],
    dict[str, str],
    list[str],
    list[str],
    dict[str, int],
    list[str],
    list[str],
    dict[str, int],
    list[str],
]:
    native_order: list[str] = []
    providers: dict[str, str] = {}
    wrappers: dict[str, str] = {}
    wrapper_filenames: dict[str, str] = {}
    ignored_aliases: list[str] = []
    ignored_alias_identities: list[str] = []
    ignored_alias_counts: dict[str, int] = {}
    wrapper_only_aliases: list[str] = []
    wrapper_only_alias_identities: list[str] = []
    wrapper_only_alias_counts: dict[str, int] = {}
    duplicates: list[str] = []
    for raw in rows:
        name = raw.strip()
        if not name:
            continue
        logical = canonical_plugin_name(name)
        folded = logical.casefold()
        if is_openmw_player_stub(name):
            wrappers[folded] = logical
            wrapper_filenames[folded] = name
            continue
        if folded in providers:
            duplicates.append(
                f"Duplicate providers for {logical!r}; using {name!r}"
            )
            providers[folded] = name
            continue
        providers[folded] = name
        native_order.append(folded)
    for folded, wrapper in wrappers.items():
        alias_filename = wrapper_filenames[folded]
        suffix = next(
            suffix
            for suffix in _OPENMW_NATIVE_SUFFIXES
            if folded.endswith(suffix)
        )
        kind = suffix.removeprefix(".")
        if folded in providers:
            ignored_alias_identities.append(folded)
            ignored_aliases.append(
                f"Ignored wrapper alias {alias_filename!r} for "
                f"{providers[folded]!r}"
            )
            ignored_alias_counts[kind] = ignored_alias_counts.get(kind, 0) + 1
        else:
            wrapper_only_alias_identities.append(folded)
            wrapper_only_aliases.append(
                f"Wrapper-only alias {alias_filename!r} for identity "
                f"{wrapper!r} is known but unavailable"
            )
            wrapper_only_alias_counts[kind] = (
                wrapper_only_alias_counts.get(kind, 0) + 1
            )
    return (
        native_order,
        providers,
        wrappers,
        ignored_aliases,
        ignored_alias_identities,
        dict(sorted(ignored_alias_counts.items())),
        wrapper_only_aliases,
        wrapper_only_alias_identities,
        dict(sorted(wrapper_only_alias_counts.items())),
        duplicates,
    )


def reconcile_plugin_inventory(
    state: OpenMWSelectionState,
    available_plugins: Iterable[str],
    primary_plugins: Iterable[str] = (),
) -> PluginInventoryReconciliation:
    """Purely reconcile canonical state with the current physical inventory."""
    validate_selection_state(state)
    candidate = _copy_v3_state(state)
    (
        _,
        providers,
        wrappers,
        ignored_aliases,
        ignored_alias_identities,
        ignored_alias_counts,
        wrapper_only_aliases,
        wrapper_only_alias_identities,
        wrapper_only_alias_counts,
        duplicates,
    ) = _inventory_providers(
        available_plugins
    )
    primary = _canonical_plugin_names(primary_plugins)
    primary_keys = [name.casefold() for name in primary]
    primary_by_key = dict(zip(primary_keys, primary))

    old_order_keys = {name.casefold() for name in candidate["plugin_order"]}
    inventory_names = dict(wrappers)
    inventory_names.update(providers)
    inventory_names.update(primary_by_key)
    new_keys = [key for key in inventory_names if key not in old_order_keys]
    new_keys.sort(key=lambda key: (key, inventory_names[key]))

    def current_casing(name: str) -> str:
        folded = name.casefold()
        return providers.get(folded, primary_by_key.get(folded, name))

    old_without_primary = [
        current_casing(name)
        for name in candidate["plugin_order"]
        if name.casefold() not in primary_by_key
    ]
    primary_prefix = [current_casing(name) for name in primary]
    new_non_primary = [
        inventory_names[key] for key in new_keys if key not in primary_by_key
    ]
    candidate["plugin_order"] = primary_prefix + old_without_primary + new_non_primary
    order_by_key = {name.casefold(): name for name in candidate["plugin_order"]}

    enabled_keys = {name.casefold() for name in candidate["enabled_plugins"]}
    enabled_keys.update(new_keys)
    enabled_keys.update(primary_keys)
    candidate["enabled_plugins"] = [
        name for name in candidate["plugin_order"] if name.casefold() in enabled_keys
    ]
    candidate["groundcover"] = [
        order_by_key[name.casefold()] for name in candidate["groundcover"]
    ]
    validate_selection_state(candidate)

    available_order = [
        name for name in candidate["plugin_order"]
        if name.casefold() in providers
    ]
    available_keys = set(providers)
    active_keys = {name.casefold() for name in candidate["enabled_plugins"]}
    insertion_positions = {
        order_by_key[key]: candidate["plugin_order"].index(order_by_key[key])
        for key in new_keys
    }
    return {
        "state": candidate,
        "available_plugins": available_order,
        "enabled_plugins": list(candidate["enabled_plugins"]),
        "active_plugins": [
            name for name in candidate["plugin_order"]
            if name.casefold() in active_keys and name.casefold() in available_keys
        ],
        "unavailable_plugins": [
            name for name in candidate["plugin_order"]
            if name.casefold() not in available_keys
        ],
        "newly_discovered": [order_by_key[key] for key in new_keys],
        "insertion_positions": insertion_positions,
        "alias_diagnostics": [*ignored_aliases, *wrapper_only_aliases],
        "ignored_alias_diagnostics": ignored_aliases,
        "ignored_alias_identities": ignored_alias_identities,
        "ignored_alias_counts": ignored_alias_counts,
        "wrapper_only_alias_diagnostics": wrapper_only_aliases,
        "wrapper_only_alias_identities": wrapper_only_alias_identities,
        "wrapper_only_alias_counts": wrapper_only_alias_counts,
        "duplicate_diagnostics": duplicates,
    }


def project_plugin_lists(
    state: OpenMWSelectionState,
    available_plugins: Iterable[str],
    primary_plugins: Iterable[str] = (),
) -> tuple[list[str], list[str]]:
    """Return canonical ``plugins.txt`` and ``loadorder.txt`` projections."""
    result = reconcile_plugin_inventory(state, available_plugins, primary_plugins)
    return result["active_plugins"], result["state"]["plugin_order"]


def read_morrowind_ini(ini_path: Path) -> MorrowindIniSelection:
    """Read legacy plugin/archive channels without interpolation or comments."""
    if not ini_path.is_file():
        return {
            "game_files": [],
            "archives": [],
            "game_files_present": False,
            "archives_present": False,
        }
    raw = ini_path.read_bytes()
    try:
        text = raw.decode("utf-8-sig", errors="strict")
    except UnicodeDecodeError:
        text = raw.decode("cp1252", errors="strict")

    entries: dict[str, dict[int, str]] = {"game files": {}, "archives": {}}
    present = {"game files": False, "archives": False}
    section: str | None = None
    patterns = {
        "game files": re.compile(r"gamefile\s*(\d+)$", re.IGNORECASE),
        "archives": re.compile(r"archive\s*(\d+)$", re.IGNORECASE),
    }
    for raw_line in text.splitlines():
        stripped = raw_line.strip()
        if not stripped or stripped.startswith(("#", ";")):
            continue
        if stripped.startswith("[") and stripped.endswith("]"):
            candidate = stripped[1:-1].strip().casefold()
            section = candidate if candidate in entries else None
            if section is not None:
                present[section] = True
            continue
        if section is None or "=" not in raw_line:
            continue
        key, value = raw_line.split("=", 1)
        match = patterns[section].fullmatch(key.strip())
        if match is None:
            continue
        index = int(match.group(1))
        value = value.strip()
        previous = entries[section].get(index)
        if previous is not None and previous != value:
            raise ValueError(
                f"Conflicting {section} index {index} in {ini_path}"
            )
        entries[section][index] = value

    return {
        "game_files": _canonical_plugin_names(
            entries["game files"][index]
            for index in sorted(entries["game files"])
            if entries["game files"][index]
        ),
        "archives": _unique_names(
            entries["archives"][index]
            for index in sorted(entries["archives"])
            if entries["archives"][index]
        ),
        "game_files_present": present["game files"],
        "archives_present": present["archives"],
    }


def read_legacy_plugin_file(path: Path) -> list[str]:
    """Read and canonicalize a MO2-style plugin list for migration only."""
    if not path.is_file():
        return []
    return _canonical_plugin_names(
        line.strip().lstrip("*").strip()
        for line in path.read_text(encoding="utf-8-sig", errors="strict").splitlines()
        if line.strip() and not line.lstrip().startswith("#")
    )


def read_legacy_archive_file(path: Path) -> list[str]:
    """Read a MO2-style archive list for migration only."""
    if not path.is_file():
        return []
    return _unique_names(
        line.strip().lstrip("*").strip()
        for line in path.read_text(encoding="utf-8-sig", errors="strict").splitlines()
        if line.strip() and not line.lstrip().startswith("#")
    )


def _coerce_openmw_selection(
    selection: OpenMWConfigSelection | dict[str, list[str]] | None,
) -> OpenMWConfigSelection:
    if selection is None:
        return {
            "content": [],
            "groundcover": [],
            "fallback_archive": [],
            "content_present": False,
            "groundcover_present": False,
            "fallback_archive_present": False,
            "plugin_order": [],
            "replaced_channels": [],
        }
    if "fallback_archive" in selection:
        return selection  # type: ignore[return-value]
    content = list(selection.get("content", []))
    groundcover = list(selection.get("groundcover", []))
    archives = list(selection.get("fallback-archive", []))
    return {
        "content": content,
        "groundcover": groundcover,
        "fallback_archive": archives,
        "content_present": bool(content),
        "groundcover_present": bool(groundcover),
        "fallback_archive_present": bool(archives),
        "plugin_order": _unique_names(content, groundcover),
        "replaced_channels": [],
    }


def migrate_selection_state(
    state: (
        OpenMWSelectionState
        | OpenMWSelectionStateV1
        | OpenMWSelectionStateV2
        | None
    ),
    *,
    available_plugins: Iterable[str],
    available_archives: Iterable[str],
    primary_plugins: Iterable[str] = (),
    morrowind_ini: MorrowindIniSelection | None = None,
    openmw_selection: OpenMWConfigSelection | dict[str, list[str]] | None = None,
    legacy_plugins: Iterable[str] = (),
    legacy_loadorder: Iterable[str] = (),
    legacy_groundcover: Iterable[str] = (),
    legacy_archives: Iterable[str] = (),
) -> OpenMWSelectionState:
    """Build v3 state using the plan's independent, fail-closed precedence."""
    available_plugins = list(available_plugins)
    available_archives = list(available_archives)
    primary_plugins = list(primary_plugins)
    parsed_cfg = _coerce_openmw_selection(openmw_selection)
    ini = morrowind_ini or {
        "game_files": [],
        "archives": [],
        "game_files_present": False,
        "archives_present": False,
    }
    legacy_plugins = _canonical_plugin_names(legacy_plugins)
    legacy_loadorder = _canonical_plugin_names(legacy_loadorder)
    legacy_groundcover = _canonical_plugin_names(legacy_groundcover)
    legacy_archives = _unique_names(legacy_archives)

    version = state.get("version") if isinstance(state, dict) else None
    if state is not None and version == _SELECTION_STATE_VERSION:
        validate_selection_state(state)
        candidate = _copy_v3_state(state)
        if candidate["plugin_state_migrated"]:
            return candidate
        candidate["plugin_state_migrated"] = True
        return candidate
    legacy_state = (
        _validate_legacy_selection_state(state) if state is not None else None
    )
    state_source = f"state-v{version}" if legacy_state is not None else None

    if legacy_state is not None:
        groundcover = _canonical_plugin_names(legacy_state["groundcover"])
        ground_source = state_source
    elif legacy_groundcover:
        groundcover = legacy_groundcover
        ground_source = "groundcover.txt"
    elif parsed_cfg["groundcover_present"]:
        groundcover = _canonical_plugin_names(parsed_cfg["groundcover"])
        ground_source = "openmw.cfg"
    else:
        groundcover = []
        ground_source = "none"
    ground_keys = {name.casefold() for name in groundcover}

    if legacy_state is not None:
        content = _canonical_plugin_names(legacy_state["enabled_plugins"])
        content_source = state_source
    elif ini["game_files_present"]:
        content = [
            name for name in _canonical_plugin_names(ini["game_files"])
            if name.casefold() not in ground_keys
        ]
        content_source = "Morrowind.ini"
    elif parsed_cfg["content_present"]:
        content = _canonical_plugin_names(parsed_cfg["content"])
        content_source = "openmw.cfg"
    elif legacy_plugins:
        content = legacy_plugins
        content_source = "plugins.txt"
    else:
        _, providers, wrappers, _, _, _, _, _, _, _ = _inventory_providers(
            available_plugins
        )
        content = _canonical_plugin_names(
            primary_plugins, providers.values(), wrappers.values()
        )
        content_source = "defaults"
    enabled = _canonical_plugin_names(content, groundcover)

    if legacy_state is not None:
        archives = _unique_names(legacy_state["archives"])
        archive_source = state_source
    elif parsed_cfg["fallback_archive_present"]:
        archives = _unique_names(parsed_cfg["fallback_archive"])
        archive_source = "openmw.cfg"
    else:
        archives = _unique_names(legacy_archives, ini["archives"])
        if legacy_archives and ini["archives"]:
            archive_source = "archives.txt+Morrowind.ini"
        elif legacy_archives:
            archive_source = "archives.txt"
        elif ini["archives"]:
            archive_source = "Morrowind.ini"
        else:
            archives = _unique_names(available_archives)
            archive_source = "defaults"

    if legacy_state is not None:
        base_order = _canonical_plugin_names(legacy_state["known_plugins"])
        order_source = state_source
    elif legacy_loadorder:
        base_order = legacy_loadorder
        order_source = "loadorder.txt"
    elif parsed_cfg["content_present"] or parsed_cfg["groundcover_present"]:
        base_order = _canonical_plugin_names(parsed_cfg["plugin_order"])
        order_source = "openmw.cfg"
    else:
        base_order = []
        order_source = "inventory"

    _, providers, wrappers, _, _, _, _, _, _, _ = _inventory_providers(
        available_plugins
    )
    inventory = sorted(
        _canonical_plugin_names(providers.values(), wrappers.values()),
        key=lambda name: (name.casefold(), name),
    )
    if order_source == "inventory":
        base_order = inventory
    completion = sorted(
        _canonical_plugin_names(inventory, enabled, groundcover),
        key=lambda name: (name.casefold(), name),
    )
    plugin_order = _canonical_plugin_names(
        primary_plugins, base_order, completion
    )
    candidate: OpenMWSelectionState = {
        "version": _SELECTION_STATE_VERSION,
        "plugin_order": plugin_order,
        "enabled_plugins": enabled,
        "groundcover": groundcover,
        "known_archives": _unique_names(available_archives, archives),
        "archives": archives,
        "profile_config_entries": (
            list(legacy_state["profile_config_entries"])
            if legacy_state is not None and version == 2 else []
        ),
        "profile_config_entries_known": (
            legacy_state["profile_config_entries_known"]
            if legacy_state is not None and version == 2 else legacy_state is None
        ),
        "profile_config_terminal": (
            legacy_state["profile_config_terminal"]
            if legacy_state is not None and version == 2 else False
        ),
        "content_migration_source": content_source,
        "groundcover_migration_source": ground_source,
        "archive_migration_source": archive_source,
        "order_migration_source": order_source,
        "plugin_state_migrated": True,
    }
    validate_selection_state(candidate)
    return reconcile_plugin_inventory(
        candidate, available_plugins, primary_plugins
    )["state"]


def create_selection_state(
    configured: dict[str, list[str]],
    loadorder: Iterable[str],
    available_plugins: Iterable[str],
    available_archives: Iterable[str],
    supplemental_archives: Iterable[str] = (),
) -> OpenMWSelectionState:
    """Compatibility adapter for legacy launch migration into version 3."""
    return migrate_selection_state(
        None,
        available_plugins=available_plugins,
        available_archives=available_archives,
        openmw_selection=configured,
        legacy_loadorder=loadorder,
        legacy_archives=supplemental_archives,
    )


def update_selection_state(
    state: OpenMWSelectionState,
    available_plugins: Iterable[str],
    available_archives: Iterable[str],
    groundcover: Iterable[str],
) -> bool:
    """Compatibility in-place update backed by pure v3 reconciliation."""
    original = json.dumps(state, sort_keys=True)
    candidate = reconcile_plugin_inventory(state, available_plugins)["state"]
    groundcover = _canonical_plugin_names(groundcover)
    order_keys = {name.casefold() for name in candidate["plugin_order"]}
    missing = [name for name in groundcover if name.casefold() not in order_keys]
    candidate["plugin_order"].extend(missing)
    casing = {name.casefold(): name for name in candidate["plugin_order"]}
    candidate["groundcover"] = [casing[name.casefold()] for name in groundcover]
    enabled_keys = {name.casefold() for name in candidate["enabled_plugins"]}
    enabled_keys.update(name.casefold() for name in groundcover)
    candidate["enabled_plugins"] = [
        name for name in candidate["plugin_order"] if name.casefold() in enabled_keys
    ]

    available_archives = list(available_archives)
    archive_provider = {name.casefold(): name for name in available_archives}
    known_keys = {name.casefold() for name in candidate["known_archives"]}
    selected_keys = {name.casefold() for name in candidate["archives"]}
    candidate["known_archives"] = [
        archive_provider.get(name.casefold(), name)
        for name in candidate["known_archives"]
    ]
    candidate["archives"] = [
        archive_provider.get(name.casefold(), name) for name in candidate["archives"]
    ]
    for name in available_archives:
        folded = name.casefold()
        if folded not in known_keys:
            known_keys.add(folded)
            candidate["known_archives"].append(name)
            if folded not in selected_keys:
                selected_keys.add(folded)
                candidate["archives"].append(name)
    validate_selection_state(candidate)
    state.clear()
    state.update(candidate)
    return original != json.dumps(state, sort_keys=True)


def read_selection_state(
    state_path: Path,
) -> (
    OpenMWSelectionState
    | OpenMWSelectionStateV1
    | OpenMWSelectionStateV2
    | None
):
    if not state_path.is_file():
        return None
    try:
        data = json.loads(state_path.read_text(encoding="utf-8"))
        if (
            isinstance(data, dict)
            and data.get("version") == _SELECTION_STATE_VERSION
        ):
            return validate_selection_state(data)
        return _validate_legacy_selection_state(data)
    except (ValueError, TypeError, json.JSONDecodeError) as error:
        raise ValueError(f"Invalid OpenMW selection state: {state_path}") from error


def upgrade_selection_state(state: dict) -> bool:
    if state.get("version") == _SELECTION_STATE_VERSION:
        validate_selection_state(state)
        return False
    upgraded = migrate_selection_state(
        state,
        available_plugins=[],
        available_archives=_unique_names(
            state.get("known_archives", []), state.get("archives", [])
        ),
    )
    state.clear()
    state.update(upgraded)
    return True


def write_selection_state(
    state_path: Path, state: OpenMWSelectionState
) -> None:
    validate_selection_state(state)
    with _atomic_text_writer(state_path) as stream:
        json.dump(state, stream, ensure_ascii=True, indent=2)
        stream.write("\n")


def _trim_trailing_blanks(lines: list[str]) -> list[str]:
    while lines and not lines[-1].strip():
        lines.pop()
    return lines


def _split_marked_blocks(
    lines: Iterable[str], begin: str, end: str
) -> tuple[list[str], list[list[str]]]:
    """Remove complete marker blocks and return their contents.

    Malformed or nested ownership markers are ambiguous, so fail without
    rewriting the file. The launch hook will then block OpenMW rather than use a
    stale profile or save path.
    """
    source = list(lines)
    out: list[str] = []
    blocks: list[list[str]] = []
    i = 0
    while i < len(source):
        marker = source[i].strip()
        if marker == end:
            raise ValueError(f"Found '{end}' without a matching '{begin}'")
        if marker != begin:
            out.append(source[i])
            i += 1
            continue
        end_index = None
        for j in range(i + 1, len(source)):
            marker = source[j].strip()
            if marker == begin:
                raise ValueError(f"Found nested '{begin}' marker")
            if marker == end:
                end_index = j
                break
        if end_index is None:
            raise ValueError(f"Found '{begin}' without a matching '{end}'")
        blocks.append(source[i + 1 : end_index])
        i = end_index + 1
    return out, blocks


def _without_marked_block(lines: Iterable[str], begin: str, end: str) -> list[str]:
    return _split_marked_blocks(lines, begin, end)[0]


def _option_value(line: str, option: str) -> str | None:
    stripped = line.strip()
    if not stripped or stripped.startswith("#") or "=" not in stripped:
        return None
    key, value = stripped.split("=", 1)
    return value.strip() if key.strip().lower() == option else None


def capture_profile_config_entries(cfg_path: Path) -> list[str]:
    """Capture raw nested config options before making a profile terminal."""
    return [
        line for line in _read_lines(cfg_path) if _option_value(line, "config") is not None
    ]


def suspend_profile_config_entries(
    state: OpenMWSelectionState, cfg_path: Path
) -> bool:
    """Save visible nested selectors and mark the profile as terminal."""
    original = json.dumps(state, sort_keys=True)
    visible = capture_profile_config_entries(cfg_path)
    if state["profile_config_terminal"]:
        saved_counts: dict[str, int] = {}
        for line in state["profile_config_entries"]:
            saved_counts[line] = saved_counts.get(line, 0) + 1
        visible_counts: dict[str, int] = {}
        for line in visible:
            visible_counts[line] = visible_counts.get(line, 0) + 1
            if visible_counts[line] > saved_counts.get(line, 0):
                state["profile_config_entries"].append(line)
    else:
        state["profile_config_entries"] = visible
    state["profile_config_terminal"] = True
    return original != json.dumps(state, sort_keys=True)


def restore_profile_config_entries(
    cfg_path: Path, entries: Iterable[str]
) -> bool:
    """Restore suspended nested selectors without duplicating visible occurrences."""
    lines = _read_lines(cfg_path)
    existing_counts: dict[str, int] = {}
    for line in capture_profile_config_entries(cfg_path):
        existing_counts[line] = existing_counts.get(line, 0) + 1

    seen: dict[str, int] = {}
    missing: list[str] = []
    for line in entries:
        seen[line] = seen.get(line, 0) + 1
        if seen[line] > existing_counts.get(line, 0):
            missing.append(line)
    if not missing:
        return False
    if lines and lines[-1].strip():
        lines.append("")
    lines.extend(missing)
    _write_lines(cfg_path, lines)
    return True


def parse_openmw_path(value: str) -> str:
    if not value.startswith('"'):
        return value.strip()
    result: list[str] = []
    escaped = False
    for character in value[1:]:
        if escaped:
            result.append(character)
            escaped = False
        elif character == "&":
            escaped = True
        elif character == '"':
            break
        else:
            result.append(character)
    if escaped:
        result.append("&")
    return "".join(result)


def read_openmw_data_dirs(cfg_path: Path) -> list[str]:
    """Read exact physical ``data=`` occurrence order from one config file."""
    result: list[str] = []
    for raw in _read_lines(cfg_path):
        line = raw.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        if key.strip().casefold() == "data" and value.strip():
            result.append(parse_openmw_path(value.strip()))
    return result


def read_profile_selector(cfg_path: Path) -> Path | None:
    """Read the profile path from Fluorine's owned root selector block."""
    _, blocks = _split_marked_blocks(
        _read_lines(cfg_path), _PROFILE_BEGIN, _PROFILE_END
    )
    destinations: list[Path] = []
    for block in blocks:
        for line in block:
            value = _option_value(line, "config")
            if value is None:
                continue
            path = Path(parse_openmw_path(value)).expanduser()
            if not path.is_absolute():
                path = cfg_path.parent / path
            destinations.append(path.resolve(strict=False))
    unique = list(dict.fromkeys(destinations))
    if len(unique) > 1:
        raise ValueError("Conflicting Fluorine OpenMW profile selectors")
    return unique[0] if unique else None


def _fsync_directory(path: Path) -> None:
    with suppress(OSError):
        directory = os.open(
            path,
            os.O_RDONLY | getattr(os, "O_DIRECTORY", 0),
        )
        try:
            os.fsync(directory)
        finally:
            os.close(directory)


@contextmanager
def _atomic_text_writer(cfg_path: Path) -> Iterator[TextIO]:
    """Yield a same-directory temporary stream and atomically replace cfg_path."""
    target = cfg_path.absolute().resolve(strict=False)
    target.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(
        prefix=f".{target.name}.", suffix=".tmp", dir=target.parent
    )
    temp_path = Path(temporary)
    try:
        with os.fdopen(fd, "w", encoding="utf-8", newline="\n") as stream:
            yield stream
            stream.flush()
            os.fsync(stream.fileno())
        if target.exists():
            shutil.copymode(target, temp_path)
            if hasattr(os, "listxattr"):
                for attribute in os.listxattr(target):
                    with suppress(OSError):
                        os.setxattr(
                            temp_path,
                            attribute,
                            os.getxattr(target, attribute),
                        )
        os.replace(temp_path, target)
        _fsync_directory(target.parent)
    except BaseException:
        temp_path.unlink(missing_ok=True)
        raise


class _FileSnapshot:
    def __init__(self, target: Path, existed: bool, backup: Path | None):
        self.target = target
        self.existed = existed
        self.backup = backup


class TransactionCleanupError(RuntimeError):
    """The transaction committed, but one or more rollback artifacts remain."""


class TransactionRollbackError(RuntimeError):
    """The transaction failed and at least one file could not be restored."""


def _transaction_target(path: Path) -> Path:
    return path.absolute().resolve(strict=False)


def validate_file_roles(roles: dict[str, Path]) -> None:
    """Reject different export roles that resolve to the same destination."""
    destinations: dict[Path, str] = {}
    for role, path in roles.items():
        target = _transaction_target(path)
        if target in destinations:
            raise ValueError(
                f"OpenMW export roles '{destinations[target]}' and '{role}' "
                f"resolve to the same file: {target}"
            )
        destinations[target] = role


def _create_file_snapshots(paths: Iterable[Path]) -> list[_FileSnapshot]:
    snapshots: list[_FileSnapshot] = []
    seen: set[Path] = set()
    try:
        for logical_path in paths:
            target = _transaction_target(logical_path)
            if target in seen:
                continue
            seen.add(target)
            if not target.parent.is_dir():
                raise ValueError(
                    f"Transaction target parent does not exist: {target.parent}"
                )
            if not target.exists():
                snapshots.append(_FileSnapshot(target, False, None))
                continue
            if not target.is_file():
                raise ValueError(f"Transaction target is not a file: {target}")

            fd, temporary = tempfile.mkstemp(
                prefix=f".{target.name}.", suffix=".rollback", dir=target.parent
            )
            os.close(fd)
            backup = Path(temporary)
            try:
                backup.unlink()
                try:
                    os.link(target, backup)
                except OSError:
                    shutil.copy2(target, backup)
            except BaseException:
                backup.unlink(missing_ok=True)
                raise
            snapshots.append(_FileSnapshot(target, True, backup))
        return snapshots
    except BaseException:
        for snapshot in snapshots:
            if snapshot.backup is not None:
                snapshot.backup.unlink(missing_ok=True)
        raise


@contextmanager
def rollback_file_changes(paths: Iterable[Path]) -> Iterator[None]:
    """Restore every listed file if a later atomic export write fails."""
    snapshots = _create_file_snapshots(paths)
    try:
        yield
    except BaseException as original:
        rollback_errors: list[str] = []
        for snapshot in reversed(snapshots):
            try:
                if snapshot.existed:
                    if snapshot.backup is None:
                        raise RuntimeError("missing rollback snapshot")
                    os.replace(snapshot.backup, snapshot.target)
                    snapshot.backup = None
                else:
                    snapshot.target.unlink(missing_ok=True)
                _fsync_directory(snapshot.target.parent)
            except BaseException as error:
                rollback_errors.append(f"{snapshot.target}: {error}")
        if rollback_errors:
            raise TransactionRollbackError(
                "OpenMW export failed and rollback was incomplete: "
                + "; ".join(rollback_errors)
            ) from original
        raise
    else:
        cleanup_errors: list[str] = []
        for snapshot in snapshots:
            if snapshot.backup is not None:
                try:
                    snapshot.backup.unlink()
                except OSError as error:
                    cleanup_errors.append(f"{snapshot.backup}: {error}")
                _fsync_directory(snapshot.target.parent)
        if cleanup_errors:
            raise TransactionCleanupError(
                "OpenMW export committed but rollback snapshot cleanup failed: "
                + "; ".join(cleanup_errors)
            )


def _write_lines(cfg_path: Path, lines: Iterable[str]) -> None:
    with _atomic_text_writer(cfg_path) as stream:
        for line in lines:
            stream.write(line)
            stream.write("\n")


def _preserve_non_managed(
    cfg_path: Path,
    *,
    strip_config: bool = False,
    strip_replace: bool = False,
) -> list[str]:
    """Return existing cfg lines minus the keys we manage, trailing blanks trimmed."""
    kept: list[str] = []
    for line in _read_lines(cfg_path):
        if _is_managed(line):
            continue
        if "=" not in line:
            kept.append(line)
            continue
        key, value = line.split("=", 1)
        folded_key = key.strip().casefold()
        if strip_config and folded_key == "config":
            continue
        if strip_replace and folded_key == "replace":
            remaining = [
                target
                for target in value.split()
                if target.casefold() not in _MANAGED_KEYS
            ]
            if not remaining:
                continue
            line = f"{key.strip()}={' '.join(remaining)}"
        kept.append(line)
    return _trim_trailing_blanks(kept)


def _write_marked_path(
    cfg_path: Path,
    value: Path | None,
    *,
    begin: str,
    end: str,
    key: str,
    strip_managed: bool = False,
) -> bool:
    original = _read_lines(cfg_path)
    had_marker = any(line.strip() in (begin, end) for line in original)
    if value is None and not strip_managed and not had_marker:
        return False
    lines = _without_marked_block(original, begin, end)
    if strip_managed:
        lines = _trim_trailing_blanks(
            [line for line in lines if not _is_managed(line)]
        )
    lines = _trim_trailing_blanks(lines)
    if value is not None:
        if lines:
            lines.append("")
        lines.extend((begin, f"{key}={escape_data_path(str(value))}", end))
    if lines == original or (not lines and not cfg_path.exists()):
        return False
    _write_lines(cfg_path, lines)
    return True


def write_profile_selector(
    cfg_path: Path,
    profile_dir: Path | None,
    *,
    strip_managed: bool = False,
    log_fn=None,
) -> None:
    """Select ``profile_dir`` as OpenMW's highest-priority config directory.

    Only the Fluorine-owned marker block is replaced; existing user ``config=``
    entries and unrelated settings are preserved. When ``strip_managed`` is
    true, data/content/archive keys previously managed in the root config are
    removed because they now live in the selected profile's openmw.cfg.
    """
    changed = _write_marked_path(
        cfg_path,
        profile_dir,
        begin=_PROFILE_BEGIN,
        end=_PROFILE_END,
        key="config",
        strip_managed=strip_managed,
    )
    if changed:
        _log = log_fn or (lambda _: None)
        if profile_dir is None:
            _log(f"  Removed Fluorine profile selector from {cfg_path}.")
        else:
            _log(f"  Selected OpenMW profile config dir: {profile_dir}.")


def write_local_saves(
    cfg_path: Path,
    user_data: Path | None,
    *,
    log_fn=None,
) -> None:
    """Set or remove profile-local saves while restoring user-owned settings."""
    original = _read_lines(cfg_path)
    lines, blocks = _split_marked_blocks(
        original, _LOCAL_SAVES_BEGIN, _LOCAL_SAVES_END
    )

    restored: list[tuple[int, str]] = []
    for block in blocks:
        for line in block:
            if line.startswith(_LOCAL_SAVES_ORIGINAL):
                payload = line[len(_LOCAL_SAVES_ORIGINAL) :]
                try:
                    index_text, encoded = payload.split(":", 1)
                    restored.append(
                        (
                            int(index_text),
                            base64.b64decode(encoded, validate=True).decode("utf-8"),
                        )
                    )
                except (ValueError, binascii.Error, UnicodeDecodeError) as e:
                    raise ValueError("Invalid saved user-data setting") from e
    seen_indexes: set[int] = set()
    for index, line in sorted(restored):
        if index < 0 or index > len(lines) or index in seen_indexes:
            raise ValueError("Invalid saved user-data position")
        seen_indexes.add(index)
        lines.insert(index, line)

    if user_data is not None:
        user_data_lines = [
            (index, line)
            for index, line in enumerate(lines)
            if "=" in line
            and line.split("=", 1)[0].strip().lower() == "user-data"
        ]
        user_data_indexes = {index for index, _ in user_data_lines}
        insertion_index = (
            sum(1 for index in range(user_data_lines[0][0]) if index not in user_data_indexes)
            if user_data_lines
            else len(lines)
        )
        lines = [
            line for index, line in enumerate(lines) if index not in user_data_indexes
        ]
        block = [_LOCAL_SAVES_BEGIN]
        block.extend(
            f"{_LOCAL_SAVES_ORIGINAL}{index}:"
            + base64.b64encode(line.encode("utf-8")).decode("ascii")
            for index, line in user_data_lines
        )
        block.extend(
            (
                f"user-data={escape_data_path(str(user_data))}",
                _LOCAL_SAVES_END,
            )
        )
        lines[insertion_index:insertion_index] = block

    if lines == original or (not lines and not cfg_path.exists()):
        return
    _write_lines(cfg_path, lines)
    if user_data is not None:
        _log = log_fn or (lambda _: None)
        _log(f"  Selected profile-local OpenMW user data dir: {user_data}.")


def _dedup_preserving_order(
    base: Iterable[str], extra: Iterable[str]
) -> list[str]:
    """Concatenate ``base`` then ``extra``, dropping case-insensitive duplicates."""
    result: list[str] = []
    seen: set[str] = set()
    for item in (*base, *extra):
        key = item.lower()
        if key not in seen:
            seen.add(key)
            result.append(item)
    return result


def _dedup_paths_preserving_order(paths: Iterable[Path | str]) -> list[str]:
    """Deduplicate paths without folding case on case-sensitive filesystems."""
    result: list[str] = []
    seen: set[str] = set()
    for path in paths:
        value = str(path)
        if value not in seen:
            seen.add(value)
            result.append(value)
    return result


def build_openmw_launcher_profile(
    data_dirs: Iterable[Path | str],
    content_plugins: Iterable[str],
    fallback_archives: Iterable[str] = (),
    *,
    vanilla_masters: Iterable[str] = VANILLA_MASTERS,
    vanilla_bsas: Iterable[str] = VANILLA_BSAS,
    profile_name: str = "Fluorine",
) -> OpenMWLauncherProfile:
    """Build the exact launcher profile projection owned by Fluorine."""
    return {
        "current_profile": profile_name,
        "data": _dedup_paths_preserving_order(data_dirs),
        "content": _dedup_preserving_order(vanilla_masters, content_plugins),
        "fallback_archive": _dedup_preserving_order(
            vanilla_bsas, fallback_archives
        ),
    }


def write_openmw_launcher_cfg(
    cfg_path: Path,
    data_dirs: Iterable[Path | str],
    content_plugins: Iterable[str],
    fallback_archives: Iterable[str] = (),
    *,
    vanilla_masters: Iterable[str] = VANILLA_MASTERS,
    vanilla_bsas: Iterable[str] = VANILLA_BSAS,
    profile_name: str = "Fluorine",
    log_fn=None,
) -> None:
    """Synchronize OpenMW Launcher's content list with Fluorine's generated one.

    The launcher stores data paths and selected content separately in
    ``launcher.cfg`` and gives that list precedence over ``openmw.cfg``. Update
    only a named Fluorine profile and the current-profile selector; unrelated
    content lists and launcher settings remain intact.
    """
    _log = log_fn or (lambda _: None)
    profile = build_openmw_launcher_profile(
        data_dirs,
        content_plugins,
        fallback_archives,
        vanilla_masters=vanilla_masters,
        vanilla_bsas=vanilla_bsas,
        profile_name=profile_name,
    )
    data = profile["data"]
    content = profile["content"]
    archives = profile["fallback_archive"]

    def write_line(stream: TextIO, line: str = "") -> None:
        stream.write(line)
        stream.write("\n")

    def write_generated_profile(stream: TextIO) -> None:
        for name in archives:
            write_line(stream, f"{profile_name}/fallback-archive={name}")
        for path in data:
            write_line(stream, f"{profile_name}/data={path}")
        for name in content:
            write_line(stream, f"{profile_name}/content={name}")

    saw_profiles = False
    saw_general = False
    in_profiles = False
    in_general = False
    general_has_first_run = False
    first_profiles_section = True

    with _atomic_text_writer(cfg_path) as output:
        def finish_section() -> None:
            nonlocal general_has_first_run, first_profiles_section
            if in_general and not general_has_first_run:
                write_line(output, "firstrun=false")
            if in_profiles and first_profiles_section:
                write_generated_profile(output)
                first_profiles_section = False

        if cfg_path.is_file():
            with cfg_path.open("r", encoding="utf-8", errors="replace") as source:
                for raw in source:
                    line = raw.rstrip("\r\n")
                    stripped = line.strip()
                    is_section = stripped.startswith("[") and stripped.endswith("]")
                    if is_section:
                        finish_section()
                        section = stripped[1:-1].strip().lower()
                        in_profiles = section == "profiles"
                        in_general = section == "general"
                        general_has_first_run = False
                        saw_profiles = saw_profiles or in_profiles
                        saw_general = saw_general or in_general
                        write_line(output, line)
                        if in_profiles and first_profiles_section:
                            write_line(output, f"currentprofile={profile_name}")
                        continue

                    if in_profiles and "=" in line:
                        key = line.split("=", 1)[0].strip()
                        profile, separator, _ = key.rpartition("/")
                        if key.lower() == "currentprofile" or (
                            separator and profile == profile_name
                        ):
                            continue
                    if in_general and "=" in line:
                        key = line.split("=", 1)[0].strip().lower()
                        general_has_first_run = general_has_first_run or key == "firstrun"
                    write_line(output, line)
            finish_section()

        if not saw_general:
            write_line(output)
            write_line(output, "[General]")
            write_line(output, "firstrun=false")
        if not saw_profiles:
            write_line(output)
            write_line(output, "[Profiles]")
            write_line(output, f"currentprofile={profile_name}")
            write_generated_profile(output)

    _log(
        f"  Wrote launcher.cfg content list: {len(data)} data dir(s) to "
        f"{cfg_path}."
    )


def read_openmw_launcher_profile(
    cfg_path: Path, profile_name: str = "Fluorine"
) -> OpenMWLauncherProfile:
    """Read one launcher's generated profile for exact write verification."""
    return inspect_openmw_launcher_cfg(cfg_path, profile_name)["profile"]


def inspect_openmw_launcher_cfg(
    cfg_path: Path, profile_name: str = "Fluorine"
) -> OpenMWLauncherInspection:
    """Stream the launcher state that Fluorine's writer owns."""
    result: OpenMWLauncherProfile = {
        "current_profile": None,
        "data": [],
        "content": [],
        "fallback_archive": [],
    }
    if not cfg_path.is_file():
        return {"profile": result, "general_initialized": False}

    in_profiles = False
    in_general = False
    saw_general = False
    general_has_first_run = False
    all_general_sections_initialized = True
    prefix = profile_name + "/"
    with cfg_path.open("r", encoding="utf-8", errors="replace") as source:
        for raw in source:
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            if line.startswith("[") and line.endswith("]"):
                if in_general and not general_has_first_run:
                    all_general_sections_initialized = False
                section = line[1:-1].strip().casefold()
                in_profiles = section == "profiles"
                in_general = section == "general"
                if in_general:
                    saw_general = True
                    general_has_first_run = False
                continue
            if "=" not in line:
                continue
            key, value = line.split("=", 1)
            key = key.strip()
            value = value.strip()
            if in_general and key.casefold() == "firstrun":
                general_has_first_run = True
            if not in_profiles:
                continue
            if key.casefold() == "currentprofile":
                result["current_profile"] = value
                continue
            if not key.startswith(prefix):
                continue
            option = key[len(prefix) :].casefold()
            if option == "data":
                result["data"].append(value)
            elif option == "content":
                result["content"].append(value)
            elif option == "fallback-archive":
                result["fallback_archive"].append(value)
    if in_general and not general_has_first_run:
        all_general_sections_initialized = False
    return {
        "profile": result,
        "general_initialized": saw_general and all_general_sections_initialized,
    }


def openmw_launcher_cfg_is_current(
    cfg_path: Path,
    expected_profile: OpenMWLauncherProfile,
) -> bool:
    """Return whether writing would change Fluorine-owned launcher state."""
    profile_name = expected_profile["current_profile"]
    if profile_name is None:
        return False
    inspection = inspect_openmw_launcher_cfg(cfg_path, profile_name)
    return (
        inspection["general_initialized"]
        and inspection["profile"] == expected_profile
    )


def build_managed_block(
    data_dirs: Iterable[Path | str],
    content_plugins: Iterable[str],
    groundcover_plugins: Iterable[str] = (),
    fallback_archives: Iterable[str] = (),
    *,
    vanilla_masters: Iterable[str] = VANILLA_MASTERS,
    vanilla_bsas: Iterable[str] = VANILLA_BSAS,
    replace_managed: bool = False,
) -> list[str]:
    """Build the managed cfg lines (no I/O), in the order OpenMW expects."""
    content = _dedup_preserving_order(vanilla_masters, content_plugins)
    archives = _dedup_preserving_order(vanilla_bsas, fallback_archives)

    block: list[str] = [""]  # blank separator from the preserved section
    if replace_managed:
        block += [f"replace={key}" for key in sorted(_REPLACED_MANAGED_KEYS)]
    block += [f"data={escape_data_path(str(d))}" for d in data_dirs]
    block += [f"content={c}" for c in content]
    block += [f"groundcover={g}" for g in groundcover_plugins]
    block += [f"fallback-archive={a}" for a in archives]
    return block


def write_openmw_cfg(
    cfg_path: Path,
    data_dirs: Iterable[Path | str],
    content_plugins: Iterable[str],
    groundcover_plugins: Iterable[str] = (),
    fallback_archives: Iterable[str] = (),
    *,
    vanilla_masters: Iterable[str] = VANILLA_MASTERS,
    vanilla_bsas: Iterable[str] = VANILLA_BSAS,
    replace_managed: bool = False,
    strip_config: bool = False,
    log_fn=None,
) -> None:
    """Rewrite the managed data=/content=/groundcover=/fallback-archive= block.

    ``strip_config`` makes this file a terminal config source. This is required
    for a selected MO2 profile because OpenMW writes settings and Lua storage to
    the final active config directory; a nested ``config=`` would otherwise
    redirect those writes away from the profile.
    """
    _log = log_fn or (lambda _: None)
    data_dirs = list(data_dirs)  # consumed twice (block + log); avoid generator exhaustion
    kept = _preserve_non_managed(
        cfg_path,
        strip_config=strip_config,
        strip_replace=replace_managed,
    )
    block = build_managed_block(
        data_dirs,
        content_plugins,
        groundcover_plugins,
        fallback_archives,
        vanilla_masters=vanilla_masters,
        vanilla_bsas=vanilla_bsas,
        replace_managed=replace_managed,
    )
    _write_lines(cfg_path, kept + block)
    _log(f"  Wrote openmw.cfg: {len(data_dirs)} data dir(s) to {cfg_path}.")


def restore_openmw_cfg(
    cfg_path: Path,
    data_dirs: Iterable[Path | str],
    *,
    vanilla_masters: Iterable[str] = VANILLA_MASTERS,
    vanilla_bsas: Iterable[str] = VANILLA_BSAS,
    log_fn=None,
) -> None:
    """Reset the managed block to vanilla-only (used on uninstall / 'clear')."""
    _log = log_fn or (lambda _: None)
    if not cfg_path.is_file():
        return
    kept = _preserve_non_managed(cfg_path)
    block = build_managed_block(
        data_dirs,
        (),
        (),
        (),
        vanilla_masters=vanilla_masters,
        vanilla_bsas=vanilla_bsas,
    )
    _write_lines(cfg_path, kept + block)
    _log(f"  Restored openmw.cfg to vanilla content at {cfg_path}.")

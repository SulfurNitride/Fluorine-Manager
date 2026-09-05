#!/usr/bin/env python3
"""Resolve a reviewed curated-guide manifest into a pinned artifact lockfile.

This is a maintainer tool.  Fluorine never invokes it during an installation.
Set NEXUS_ACCESS_TOKEN (OAuth) or NEXUS_API_KEY before running it.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import urllib.error
import urllib.request
from pathlib import Path


VIRUSTOTAL_SHA256 = re.compile(
    r"https://www\.virustotal\.com/(?:gui/)?file/([0-9a-fA-F]{64})"
)
CATEGORY_IDS = {
    "main files": {1},
    "updates": {2},
    "optional files": {3},
    "miscellaneous files": {5},
}
CATEGORY_NAMES = {
    "main files": {"main", "main files"},
    "updates": {"update", "updates"},
    "optional files": {"optional", "optional files"},
    "miscellaneous files": {"misc", "miscellaneous", "miscellaneous files"},
}


def normalized_label(value: str) -> str:
    return re.sub(r"[^a-z0-9]+", "", value.lower().replace("xnvse", "nvse"))


def version_parts(value: str) -> tuple[int, ...]:
    return tuple(int(part) for part in re.findall(r"\d+", value))


def version_allowed(value: str, minimum: str) -> bool:
    if not minimum:
        return True
    current = version_parts(value)
    wanted = version_parts(minimum)
    width = max(len(current), len(wanted))
    return current + (0,) * (width - len(current)) >= wanted + (0,) * (width - len(wanted))


def category_matches(file: dict, guide_category: str) -> bool:
    category = guide_category.lower()
    if not category:
        return True
    return (file.get("category_id") in CATEGORY_IDS.get(category, set())
            or str(file.get("category_name", "")).lower()
            in CATEGORY_NAMES.get(category, set()))


class NexusClient:
    def __init__(self) -> None:
        token = os.environ.get("NEXUS_ACCESS_TOKEN", "")
        api_key = os.environ.get("NEXUS_API_KEY", "")
        if token:
            self.auth_header = ("Authorization", f"Bearer {token}")
        elif api_key:
            self.auth_header = ("APIKEY", api_key)
        else:
            raise SystemExit("set NEXUS_ACCESS_TOKEN or NEXUS_API_KEY")
        self.cache: dict[tuple[str, int], list[dict]] = {}

    def files(self, domain: str, mod_id: int) -> list[dict]:
        key = (domain, mod_id)
        if key in self.cache:
            return self.cache[key]
        request = urllib.request.Request(
            f"https://api.nexusmods.com/v1/games/{domain}/mods/{mod_id}/files.json",
            headers={self.auth_header[0]: self.auth_header[1],
                     "User-Agent": "Fluorine curated-guide lock compiler"},
        )
        try:
            with urllib.request.urlopen(request, timeout=30) as response:
                files = json.load(response).get("files", [])
        except urllib.error.HTTPError as error:
            raise RuntimeError(f"Nexus returned HTTP {error.code} for {domain}/{mod_id}") from error
        self.cache[key] = files
        return files


def select_file(artifact: dict, files: list[dict]) -> dict:
    pinned_id = int(artifact.get("fileId", 0))
    if pinned_id:
        matches = [file for file in files if file.get("file_id") == pinned_id]
    else:
        allowed = [
            file for file in files
            if version_allowed(str(file.get("version", "")), artifact.get("minimumVersion", ""))
        ]
        if artifact.get("latestCompatible"):
            matches = [file for file in allowed if category_matches(file, "main files")]
        else:
            label = artifact.get("fileLabel", "")
            matches = [
                file for file in allowed
                if str(file.get("name", "")).lower() == label.lower()
                and category_matches(file, artifact.get("fileCategory", ""))
            ]
            if not matches:
                matches = [
                    file for file in allowed
                    if normalized_label(str(file.get("name", ""))) == normalized_label(label)
                    and category_matches(file, artifact.get("fileCategory", ""))
                ]
            if not matches:
                matches = [
                    file for file in allowed
                    if normalized_label(str(file.get("name", ""))) == normalized_label(label)
                ]
    if not matches:
        raise ValueError(
            f"{artifact['id']}: no file matches {artifact.get('fileLabel', '')!r} "
            f"in {artifact.get('fileCategory', '')!r}"
        )
    return max(matches, key=lambda file: int(file.get("file_id", 0)))


def compile_lock(manifest: dict, nexus: NexusClient) -> dict:
    locked: dict[str, dict] = {}
    errors: list[str] = []
    for artifact in manifest.get("artifacts", []):
        if artifact.get("source") != "nexus":
            continue
        try:
            selected = select_file(
                artifact, nexus.files(artifact["domain"], int(artifact["modId"]))
            )
        except (RuntimeError, ValueError) as error:
            errors.append(str(error))
            continue
        virus_url = selected.get("external_virus_scan_url", "") or ""
        hash_match = VIRUSTOTAL_SHA256.match(virus_url)
        entry = {
            "domain": artifact["domain"],
            "modId": int(artifact["modId"]),
            "fileId": int(selected["file_id"]),
            "name": selected.get("name", ""),
            "filename": selected.get("file_name", ""),
            "version": selected.get("version", ""),
            "size": int(selected.get("size_in_bytes") or 0),
        }
        if hash_match:
            entry["sha256"] = hash_match.group(1).lower()
        locked[artifact["id"]] = entry
    if errors:
        raise SystemExit("could not lock every Nexus artifact:\n  " + "\n  ".join(errors))
    return {
        "schemaVersion": 1,
        "recipeId": manifest["id"],
        "recipeVersion": manifest["version"],
        "sourceCommit": manifest.get("sourceCommit", ""),
        "artifacts": locked,
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    rendered = json.dumps(compile_lock(manifest, NexusClient()), indent=2,
                          ensure_ascii=False) + "\n"
    if args.check:
        if not args.output.is_file() or args.output.read_text(encoding="utf-8") != rendered:
            raise SystemExit("generated lock differs from the checked-in lock")
        return
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(rendered, encoding="utf-8")


if __name__ == "__main__":
    main()

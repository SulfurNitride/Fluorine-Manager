#!/usr/bin/env python3
"""Collect license texts for the target-reachable packages of a Cargo graph."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess


LICENSE_PREFIXES = ("license", "copying", "notice")

# rust-zstd publishes its BSD notice below src/ and embeds the MIT notice for
# its ruzstd-derived decoder at the start of the source file. Cargo therefore
# exposes no top-level license file for this exact package even though both
# required notices are present in the crate archive.
PACKAGE_LICENSE_SUPPLEMENTS = {
    ("rust-zstd", "0.1.0", "BSD-3-Clause AND MIT"): (
        "src/LICENSE-ZSTD",
        "src/decode.rs",
    ),
}


def license_files(package_root: Path) -> list[Path]:
    return sorted(
        path
        for path in package_root.iterdir()
        if path.is_file() and path.name.lower().startswith(LICENSE_PREFIXES)
    )


def package_license_files(package: dict) -> list[Path]:
    package_root = Path(package["manifest_path"]).parent
    candidates = license_files(package_root)
    if not candidates and package.get("source") is None:
        for parent in package_root.parents:
            candidates = license_files(parent)
            if candidates:
                break

    supplement_key = (
        package["name"],
        package["version"],
        package.get("license") or "",
    )
    for relative_path in PACKAGE_LICENSE_SUPPLEMENTS.get(supplement_key, ()):
        source = package_root / relative_path
        if not source.is_file():
            raise RuntimeError(
                f"Cargo package {package['name']} {package['version']} is missing "
                f"supplemental license material {relative_path}"
            )
        candidates.append(source)

    return sorted(set(candidates))


def target_triple() -> str:
    result = subprocess.run(
        ["rustc", "-vV"], check=True, capture_output=True, text=True
    )
    for line in result.stdout.splitlines():
        if line.startswith("host: "):
            return line.removeprefix("host: ")
    raise RuntimeError("rustc did not report its host target")


def cargo_metadata(manifest: Path) -> dict:
    result = subprocess.run(
        [
            "cargo",
            "metadata",
            "--format-version",
            "1",
            "--locked",
            "--filter-platform",
            target_triple(),
            "--manifest-path",
            str(manifest),
        ],
        check=True,
        capture_output=True,
        text=True,
        timeout=60,
    )
    return json.loads(result.stdout)


def reachable_package_ids(metadata: dict) -> set[str]:
    resolution = metadata["resolve"]
    nodes = {node["id"]: node for node in resolution["nodes"]}
    pending = [resolution["root"]]
    reachable: set[str] = set()
    while pending:
        package_id = pending.pop()
        if package_id in reachable:
            continue
        reachable.add(package_id)
        pending.extend(dependency["pkg"] for dependency in nodes[package_id]["deps"])
    return reachable


def safe_name(value: str) -> str:
    return re.sub(r"[^A-Za-z0-9._+-]", "_", value)


def collect_manifest(manifest: Path, output: Path) -> int:
    metadata = cargo_metadata(manifest.resolve())
    reachable = reachable_package_ids(metadata)
    copied = 0
    for package in sorted(
        (package for package in metadata["packages"] if package["id"] in reachable),
        key=lambda package: (package["name"], package["version"], package["id"]),
    ):
        candidates = package_license_files(package)
        if not candidates:
            raise RuntimeError(
                f"Cargo package {package['name']} {package['version']} has no license file"
            )

        package_output = output / safe_name(
            f"{package['name']}-{package['version']}"
        )
        package_output.mkdir(parents=True, exist_ok=True)
        for source in candidates:
            destination = package_output / safe_name(source.name)
            if destination.exists():
                if destination.read_bytes() != source.read_bytes():
                    raise RuntimeError(f"conflicting Cargo license: {destination}")
                continue
            shutil.copyfile(source, destination)
            copied += 1
    return copied


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("manifests", nargs="+", type=Path)
    arguments = parser.parse_args()

    arguments.output.mkdir(parents=True, exist_ok=True)
    try:
        copied = sum(
            collect_manifest(manifest, arguments.output)
            for manifest in arguments.manifests
        )
    except (OSError, RuntimeError, subprocess.SubprocessError, ValueError) as error:
        parser.exit(1, f"ERROR: Cargo license collection failed: {error}\n")
    if copied == 0:
        parser.exit(1, "ERROR: Cargo license collection produced no files\n")
    print(f"Collected {copied} Cargo license files into {arguments.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

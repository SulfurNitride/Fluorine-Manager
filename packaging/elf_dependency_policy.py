#!/usr/bin/env python3
"""Validate the dynamic dependency boundary of a staged Fluorine bundle.

The bundle is trusted build output, so invoking ``ldd`` here is safe.  Runtime
libraries intentionally supplied by the host are kept on a small SONAME
allowlist; every other dependency must resolve inside the bundle.
"""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import re
import subprocess
import sys
from dataclasses import dataclass


HOST_SONAME_PATTERNS = tuple(
    re.compile(pattern)
    for pattern in (
        r"linux-vdso\.so\.\d+",
        r"ld-linux-[^.]+\.so\.\d+",
        r"lib(c|m|dl|rt|pthread|resolv|util)\.so\.\d+",
        r"libnss_[A-Za-z0-9_-]+\.so\.\d+",
        r"libgcc_s\.so\.\d+",
        r"libstdc\+\+\.so\.\d+",
        r"lib(EGL|GL|GLX|GLdispatch|drm|gbm|vulkan)\.so\.\d+",
        r"libX11(-xcb)?\.so\.\d+",
        r"libxcb\.so\.\d+",
        r"libxcb-(glx|randr|render|shape|shm|sync|xfixes|xkb)\.so\.\d+",
        r"libxkbcommon(-x11)?\.so\.\d+",
        r"libwayland-(client|cursor|egl|server)\.so\.\d+",
        r"libfontconfig\.so\.\d+",
        r"lib(ssl|crypto)\.so\.\d+",
    )
)


@dataclass(frozen=True)
class Dependency:
    soname: str
    path: str | None


def parse_ldd_output(output: str) -> list[Dependency]:
    dependencies: list[Dependency] = []
    for raw_line in output.splitlines():
        line = raw_line.strip()
        if not line or line in {"statically linked", "not a dynamic executable"}:
            continue

        if "=>" in line:
            soname, resolution = (part.strip() for part in line.split("=>", 1))
            if resolution.startswith("not found"):
                dependencies.append(Dependency(soname, None))
                continue
            path = resolution.split(" (", 1)[0].strip()
            dependencies.append(Dependency(soname, path))
            continue

        path = line.split(" (", 1)[0].strip()
        if path.startswith("/"):
            dependencies.append(Dependency(Path(path).name, path))
        else:
            soname = path.split(maxsplit=1)[0]
            if soname.startswith("linux-vdso"):
                dependencies.append(Dependency(soname, "/host/" + soname))

    return dependencies


def is_allowed_host_soname(soname: str) -> bool:
    return any(pattern.fullmatch(soname) for pattern in HOST_SONAME_PATTERNS)


def dependency_errors(
    bundle_root: Path, object_path: Path, dependencies: list[Dependency]
) -> list[str]:
    root = bundle_root.resolve()
    errors: list[str] = []
    for dependency in dependencies:
        if dependency.path is None:
            errors.append(f"{object_path}: missing {dependency.soname}")
            continue

        resolved = Path(dependency.path).resolve(strict=False)
        if resolved.is_relative_to(root):
            continue
        if not is_allowed_host_soname(dependency.soname):
            errors.append(
                f"{object_path}: unexpected host dependency "
                f"{dependency.soname} => {dependency.path}"
            )
    return errors


def bundled_host_runtime_errors(bundle_root: Path) -> list[str]:
    library_root = bundle_root.resolve() / "lib"
    if not library_root.is_dir():
        return []

    return [
        f"host runtime must not be bundled: {path.name}"
        for path in sorted(library_root.iterdir())
        if is_allowed_host_soname(path.name)
    ]


def iter_elf_files(bundle_root: Path):
    for path in sorted(bundle_root.rglob("*")):
        if path.is_symlink() or not path.is_file():
            continue
        try:
            with path.open("rb") as stream:
                if stream.read(4) == b"\x7fELF":
                    yield path
        except OSError as error:
            raise RuntimeError(f"cannot inspect {path}: {error}") from error


def validate_bundle(bundle_root: Path) -> list[str]:
    root = bundle_root.resolve()
    if not root.is_dir():
        return [f"bundle root is not a directory: {bundle_root}"]

    environment = os.environ.copy()
    environment["LD_LIBRARY_PATH"] = str(root / "lib")
    errors = bundled_host_runtime_errors(root)
    for path in iter_elf_files(root):
        result = subprocess.run(
            ["ldd", str(path)],
            check=False,
            capture_output=True,
            text=True,
            timeout=30,
            env=environment,
        )
        output = result.stdout + result.stderr
        if result.returncode != 0 and "not a dynamic executable" not in output:
            errors.append(f"{path}: ldd failed ({result.returncode}): {output.strip()}")
            continue
        errors.extend(dependency_errors(root, path, parse_ldd_output(output)))
    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("bundle_root", type=Path)
    arguments = parser.parse_args()

    try:
        errors = validate_bundle(arguments.bundle_root)
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"ERROR: dependency validation failed: {error}", file=sys.stderr)
        return 1

    if errors:
        for error in errors:
            print(f"ERROR: {error}", file=sys.stderr)
        return 1

    print(f"ELF dependency closure verified: {arguments.bundle_root}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

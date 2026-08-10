"""Executable lifetime rules for native and Flatpak OpenMW launches."""

from __future__ import annotations

from pathlib import Path


def companion_process_names(
    executable: str, arguments: list[str], flatpak_id: str
) -> list[str]:
    """Return child process names that must outlive the selected wrapper."""
    binary = Path(executable).name.lower()
    args = [argument.lower() for argument in arguments]

    if binary == "openmw-launcher":
        return ["openmw"]

    if binary == "flatpak" and flatpak_id.lower() in args:
        if "--command=openmw-launcher" in args:
            return ["openmw-launcher", "openmw"]
        return ["openmw"]

    return []

#!/usr/bin/env python3
"""Render Fluorine's installed desktop entry without invoking a shell."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import tempfile


EXEC_TEMPLATE = "Exec=fluorine-manager %u"


def escape_exec_argument(value: str) -> str:
    if any(character in value for character in ("\0", "\r", "\n")):
        raise ValueError("desktop Exec path contains a control character")

    escaped = value.replace("\\", "\\\\")
    for character in ('"', "`", "$"):
        escaped = escaped.replace(character, "\\" + character)
    escaped = escaped.replace("%", "%%")
    return f'"{escaped}"'


def render_desktop_entry(source: Path, target: Path, launcher: Path) -> None:
    source_text = source.read_text(encoding="utf-8")
    lines = source_text.splitlines()
    if lines.count(EXEC_TEMPLATE) != 1:
        raise ValueError("desktop template must contain exactly one launcher Exec line")

    rendered_line = f"Exec={escape_exec_argument(str(launcher))} %u"
    rendered = "\n".join(
        rendered_line if line == EXEC_TEMPLATE else line for line in lines
    ) + "\n"

    target.parent.mkdir(parents=True, exist_ok=True)
    temporary_path: Path | None = None
    try:
        descriptor, name = tempfile.mkstemp(
            dir=target.parent, prefix=f".{target.name}.", text=True
        )
        temporary_path = Path(name)
        with os.fdopen(descriptor, "w", encoding="utf-8", newline="\n") as stream:
            stream.write(rendered)
            stream.flush()
            os.fsync(stream.fileno())
        temporary_path.chmod(0o755)
        os.replace(temporary_path, target)
        temporary_path = None

        directory_descriptor = os.open(target.parent, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(directory_descriptor)
        finally:
            os.close(directory_descriptor)
    finally:
        if temporary_path is not None:
            temporary_path.unlink(missing_ok=True)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("target", type=Path)
    parser.add_argument("launcher", type=Path)
    arguments = parser.parse_args()

    try:
        render_desktop_entry(arguments.source, arguments.target, arguments.launcher)
    except (OSError, UnicodeError, ValueError) as error:
        parser.exit(1, f"ERROR: cannot install desktop entry: {error}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

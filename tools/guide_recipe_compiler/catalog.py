#!/usr/bin/env python3
"""Refresh hashes in the curated-guide catalog after reviewed updates."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--catalog", type=Path, required=True)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    catalog = json.loads(args.catalog.read_text(encoding="utf-8"))
    for entry in catalog["recipes"]:
        entry["manifestSha256"] = sha256(args.catalog.parent / entry["manifest"])
        if entry.get("lock"):
            entry["lockSha256"] = sha256(args.catalog.parent / entry["lock"])
    rendered = json.dumps(catalog, indent=2, ensure_ascii=False) + "\n"
    if args.check:
        if args.catalog.read_text(encoding="utf-8") != rendered:
            raise SystemExit("curated-guide catalog hashes are stale")
        return
    args.catalog.write_text(rendered, encoding="utf-8")


if __name__ == "__main__":
    main()

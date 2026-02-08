#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
DEST_DIR="$REPO_ROOT/crates/mo2gui/bin"
DEST_BIN="$DEST_DIR/7zz"

PAGE_URL="https://7-zip.org/download.html"

echo "Fetching latest Linux x64 7-Zip download URL from $PAGE_URL"
HTML="$(curl -fsSL "$PAGE_URL")"
REL_PATH="$(printf '%s' "$HTML" | grep -oE 'a/7z[0-9]+-linux-x64\.tar\.xz' | head -n1 || true)"

if [[ -z "$REL_PATH" ]]; then
  echo "Failed to locate Linux x64 download URL on 7-zip.org" >&2
  exit 1
fi

ARCHIVE_URL="https://7-zip.org/${REL_PATH}"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

ARCHIVE_PATH="$TMP_DIR/7zz-linux-x64.tar.xz"

echo "Downloading $ARCHIVE_URL"
curl -fL "$ARCHIVE_URL" -o "$ARCHIVE_PATH"

echo "Extracting archive"
tar -xJf "$ARCHIVE_PATH" -C "$TMP_DIR"

if [[ ! -f "$TMP_DIR/7zz" ]]; then
  echo "Archive did not contain expected 7zz binary" >&2
  exit 1
fi

mkdir -p "$DEST_DIR"
install -m 755 "$TMP_DIR/7zz" "$DEST_BIN"

echo "Installed bundled 7zz to $DEST_BIN"
"$DEST_BIN" i | sed -n '1,2p'

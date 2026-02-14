#!/usr/bin/env bash
# build-flatpak.sh — Build the Flatpak package.
# Usage: ./build-flatpak.sh [install|bundle]   (default: install)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
MODE="${1:-install}"

exec bash "${SCRIPT_DIR}/flatpak/flatpak-install.sh" "${MODE}"

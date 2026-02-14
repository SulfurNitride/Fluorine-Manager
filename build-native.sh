#!/usr/bin/env bash
# build-native.sh — Build and install Fluorine Manager natively (non-Flatpak).
# Uses a container to compile, then installs to ~/.local/share/fluorine/.
# Override container engine with CONTAINER_ENGINE=docker.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
IMAGE_NAME="fluorine-builder"
CONTAINER_ENGINE="${CONTAINER_ENGINE:-podman}"
INSTALL_DIR="${HOME}/.local/share/fluorine"
STAGING="${SCRIPT_DIR}/build-container/staging"

# ── Build the container image if it doesn't exist ──
if ! ${CONTAINER_ENGINE} image exists "${IMAGE_NAME}" 2>/dev/null; then
    echo "Building ${IMAGE_NAME} image (one-time)..."
    ${CONTAINER_ENGINE} build -t "${IMAGE_NAME}" -f "${SCRIPT_DIR}/docker/Dockerfile" "${SCRIPT_DIR}/docker"
fi

# ── Ensure build dir exists (Podman needs it before mounting) ──
mkdir -p "${SCRIPT_DIR}/build-container"

# ── Run the build inside the container ──
echo "Building Fluorine Manager inside container..."
${CONTAINER_ENGINE} run --rm \
    -v "${SCRIPT_DIR}:/src:Z" \
    -v "${SCRIPT_DIR}/build-container:/src/build:Z" \
    -w /src \
    "${IMAGE_NAME}" \
    bash docker/build-inner.sh

if [ ! -d "${STAGING}" ]; then
    echo "ERROR: Staging directory not found at ${STAGING}"
    exit 1
fi

# ── Install to ~/.local/share/fluorine/ ──
echo ""
echo "Installing to ${INSTALL_DIR}/ ..."
mkdir -p "${INSTALL_DIR}"

# Remove dangling symlinks left by the Flatpak wrapper (they point to /app/
# which doesn't exist outside the sandbox) and stale symlinks that conflict
# with directories from the staging area.
find "${INSTALL_DIR}" -maxdepth 3 -type l ! -exec test -e {} \; -delete 2>/dev/null || true
for d in plugins/libs plugins/dlls plugins/data; do
    [ -L "${INSTALL_DIR}/${d}" ] && rm -f "${INSTALL_DIR}/${d}"
done

# Copy all files, preserving structure.  Existing user data (Prefix/, logs/,
# config/, instances) won't be touched because they're in subdirs that the
# staging area doesn't contain.
cp -af "${STAGING}/." "${INSTALL_DIR}/"

# ── Desktop entry ──
DESKTOP_DIR="${HOME}/.local/share/applications"
mkdir -p "${DESKTOP_DIR}"
cat > "${DESKTOP_DIR}/fluorine-manager.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=Fluorine Manager
Comment=Mod Organizer 2 for Linux
Exec=${INSTALL_DIR}/fluorine-manager
Icon=fluorine-manager
Terminal=false
Categories=Game;
EOF

# ── Symlink into ~/.local/bin for PATH access ──
BIN_DIR="${HOME}/.local/bin"
mkdir -p "${BIN_DIR}"
ln -sf "${INSTALL_DIR}/fluorine-manager" "${BIN_DIR}/fluorine-manager"

echo ""
echo "=== Installed ==="
du -sh "${INSTALL_DIR}"/*/ "${INSTALL_DIR}"/ModOrganizer-core 2>/dev/null | sort -rh
echo ""
echo "Fluorine Manager installed to: ${INSTALL_DIR}/"
echo "Run with: fluorine-manager  (or find it in your app launcher)"

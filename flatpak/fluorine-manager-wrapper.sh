#!/bin/bash
# Flatpak wrapper for Fluorine Manager (MO2 Linux)
# Sets up a writable overlay so users can add custom plugins while still
# loading the bundled ones from the read-only /app tree.
#
# For portable instances (--instance /path), the overlay is created INSIDE the
# instance directory so each portable install is fully self-contained.
# For the global instance (no --instance), a shared overlay is used.

BUNDLED="/app/lib/fluorine"

# ── Detect --instance argument ──
# If launching a portable instance, use its directory as the overlay target.
INSTANCE_DIR=""
PREV=""
for arg in "$@"; do
    if [ "$PREV" = "--instance" ] || [ "$PREV" = "-i" ]; then
        INSTANCE_DIR="$arg"
        break
    fi
    PREV="$arg"
done

if [ -n "$INSTANCE_DIR" ] && [ -d "$INSTANCE_DIR" ]; then
    # Portable instance: overlay goes into the instance directory itself.
    # Everything is self-contained: mods, profiles, plugins, dlls, libs.
    USER_DIR="$INSTANCE_DIR"
else
    # Global instance: shared overlay at ~/.local/share/fluorine/
    # Use $HOME directly to bypass Flatpak's XDG_DATA_HOME remapping.
    USER_DIR="$HOME/.local/share/fluorine"
fi

# ── Create writable overlay with symlinks to bundled files ──
# This lets MO2 load bundled plugins AND any custom ones the user drops in.
# Existing files are never overwritten (user overrides take priority).
setup_overlay() {
    mkdir -p "${USER_DIR}/plugins" "${USER_DIR}/dlls" "${USER_DIR}/lib"

    # Symlink bundled plugins (skip existing - user overrides take priority)
    for f in "${BUNDLED}/plugins/"*; do
        [ -e "$f" ] || continue
        local base="$(basename "$f")"
        local target="${USER_DIR}/plugins/${base}"
        [ -e "$target" ] || [ -L "$target" ] || ln -sf "$f" "$target"
    done

    # Symlink bundled dlls
    for f in "${BUNDLED}/dlls/"*; do
        [ -e "$f" ] || continue
        local base="$(basename "$f")"
        local target="${USER_DIR}/dlls/${base}"
        [ -e "$target" ] || [ -L "$target" ] || ln -sf "$f" "$target"
    done

    # Symlink bundled libs
    for f in "${BUNDLED}/lib/"*; do
        [ -e "$f" ] || continue
        local base="$(basename "$f")"
        local target="${USER_DIR}/lib/${base}"
        [ -e "$target" ] || [ -L "$target" ] || ln -sf "$f" "$target"
    done

    # Symlink other bundled files (binaries, tools) directly
    for f in ModOrganizer-core lootcli wrestool icotool fusermount3 cabextract; do
        [ -e "${BUNDLED}/$f" ] || continue
        [ -e "${USER_DIR}/$f" ] || [ -L "${USER_DIR}/$f" ] || ln -sf "${BUNDLED}/$f" "${USER_DIR}/$f"
    done

    # umu-run must be a real copy (not symlink to /app/) because it runs on
    # the host via flatpak-spawn --host, where /app/ doesn't exist.
    # Remove any stale symlink first (cp -f follows symlinks, won't replace them).
    if [ -e "${BUNDLED}/umu-run" ]; then
        rm -f "${USER_DIR}/umu-run"
        cp "${BUNDLED}/umu-run" "${USER_DIR}/umu-run"
        chmod +x "${USER_DIR}/umu-run"
    fi

    # VFS helper must be a real binary copy (not a symlink to /app/) because
    # it runs on the host via flatpak-spawn --host, where /app/ doesn't exist.
    # libfuse3 is statically linked, so no extra .so files needed.
    VFS_HELPER_DIR="$HOME/.local/share/fluorine/bin"
    if [ -e "${BUNDLED}/mo2-vfs-helper" ]; then
        mkdir -p "${VFS_HELPER_DIR}"
        cp -f "${BUNDLED}/mo2-vfs-helper" "${VFS_HELPER_DIR}/mo2-vfs-helper"
        chmod +x "${VFS_HELPER_DIR}/mo2-vfs-helper"
    fi
}

setup_overlay

export PATH="${USER_DIR}:${BUNDLED}:${PATH}"
export LD_LIBRARY_PATH="${USER_DIR}/lib:${BUNDLED}/lib:${BUNDLED}/python/lib:${LD_LIBRARY_PATH:-}"

# MO2 resolves plugins/dlls relative to MO2_BASE_DIR (or basePath()).
# Point it at the overlay so custom plugins are found.
export MO2_BASE_DIR="${USER_DIR}"
export MO2_PLUGINS_DIR="${USER_DIR}/plugins"
export MO2_DLLS_DIR="${USER_DIR}/dlls"
export MO2_PYTHON_DIR="${BUNDLED}/python"

# Do NOT set PYTHONHOME globally -- it leaks into child processes (umu-run,
# Proton, winetricks) and breaks their Python.  The plugin_python runner reads
# MO2_PYTHON_DIR and sets PYTHONHOME internally before loading the interpreter.
unset PYTHONHOME PYTHONPATH PYTHONNOUSERSITE

# Qt6 plugins from KDE runtime.
export QT_PLUGIN_PATH="/usr/lib/plugins"

exec "${BUNDLED}/ModOrganizer-core" "$@"

#!/usr/bin/env bash
set -euo pipefail

BUILD_PY="${BUILD_PYTHON:-$(command -v python3)}"

# ── Build ──
PYBIND11_DIR="$("${BUILD_PY}" -c 'import pybind11; print(pybind11.get_cmake_dir())' 2>/dev/null || true)"

CMAKE_EXTRA_ARGS=()

# Enable ccache if available and not explicitly overridden.
if [ -z "${CMAKE_C_COMPILER_LAUNCHER:-}" ] && command -v ccache >/dev/null 2>&1; then
    CMAKE_C_COMPILER_LAUNCHER=ccache
    CMAKE_CXX_COMPILER_LAUNCHER=ccache
fi
if [ -n "${CMAKE_C_COMPILER_LAUNCHER:-}" ]; then
    CMAKE_EXTRA_ARGS+=("-DCMAKE_C_COMPILER_LAUNCHER=${CMAKE_C_COMPILER_LAUNCHER}")
fi
if [ -n "${CMAKE_CXX_COMPILER_LAUNCHER:-}" ]; then
    CMAKE_EXTRA_ARGS+=("-DCMAKE_CXX_COMPILER_LAUNCHER=${CMAKE_CXX_COMPILER_LAUNCHER}")
fi

PYTHON_ROOT="$(dirname "$(dirname "${BUILD_PY}")")"

# Forward version/channel settings from the CI workflow (or local overrides).
# Defaults: stable channel, empty timestamp/commit (CMake fills commit from git).
FLUORINE_BUILD_CHANNEL="${FLUORINE_BUILD_CHANNEL:-stable}"
FLUORINE_BUILD_TIMESTAMP="${FLUORINE_BUILD_TIMESTAMP:-}"
FLUORINE_BUILD_COMMIT="${FLUORINE_BUILD_COMMIT:-}"

cmake -S . -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DPython_EXECUTABLE="${BUILD_PY}" \
    -DPython_ROOT_DIR="${PYTHON_ROOT}" \
    ${PYBIND11_DIR:+-Dpybind11_DIR="${PYBIND11_DIR}"} \
    -DBUILD_PLUGIN_PYTHON=ON \
    -DFLUORINE_BUILD_CHANNEL="${FLUORINE_BUILD_CHANNEL}" \
    -DFLUORINE_BUILD_TIMESTAMP="${FLUORINE_BUILD_TIMESTAMP}" \
    -DFLUORINE_BUILD_COMMIT="${FLUORINE_BUILD_COMMIT}" \
    "${CMAKE_EXTRA_ARGS[@]}"

cmake --build build --parallel "${BUILD_JOBS:-}"

MODORG_BIN="build/src/src/ModOrganizer"
if [ ! -f "${MODORG_BIN}" ]; then
    echo "ERROR: ModOrganizer binary not found at ${MODORG_BIN}"
    exit 1
fi
RUNDIR="build/src/src"

# ── Output layout (staging area — installed to ~/.local/share/fluorine by build-native.sh) ──
OUT_DIR="/src/build/staging"
rm -rf "${OUT_DIR}"
mkdir -p "${OUT_DIR}/plugins" "${OUT_DIR}/lib"

# ── Main binary + helpers ──
cp -f "${RUNDIR}/ModOrganizer" "${OUT_DIR}/ModOrganizer-core"
[ -f "${RUNDIR}/README-PORTABLE.txt" ] && cp -f "${RUNDIR}/README-PORTABLE.txt" "${OUT_DIR}/"
[ -f "/src/src/fluorine-manager" ] && cp -f "/src/src/fluorine-manager" "${OUT_DIR}/"

# wrestool/icotool no longer needed — icon extraction is built into the C++ PE parser

# ── MO2 plugins (.so) ──
find build/libs -type f \( \
    -name "libgame_*.so" -o \
    -name "libinstaller_*.so" -o \
    -name "libfomod_plus_*.so" -o \
    -name "libpreview_*.so" -o \
    -name "libdiagnose_*.so" -o \
    -name "libcheck_*.so" -o \
    -name "libskse_*.so" -o \
    -name "libtool_*.so" -o \
    -name "libinieditor.so" -o \
    -name "libinibakery.so" -o \
    -name "libbsa_extractor.so" -o \
    -name "libbsa_packer.so" -o \
    -name "libbsplugins.so" -o \
    -name "libproxy.so" \
\) -exec cp -f {} "${OUT_DIR}/plugins/" \;

# Python plugin loader (small — kept for optional Python support).
[ -f "build/src/src/plugins/libplugin_python.so" ] && cp -f "build/src/src/plugins/libplugin_python.so" "${OUT_DIR}/plugins/"
# mobase pybind11 module — the Python proxy expects it in plugins/libs/.
if [ -d "build/src/src/plugins/libs" ]; then
    mkdir -p "${OUT_DIR}/plugins/libs"
    cp -f build/src/src/plugins/libs/mobase*.so "${OUT_DIR}/plugins/libs/" 2>/dev/null || true
fi
# Python helper shims — copy from source directly (cmake staging is OFF by default)
for f in lzokay.py winreg.py pyCfg.py; do
    [ -f "libs/${f}" ] && cp -f "libs/${f}" "${OUT_DIR}/plugins/"
    [ -f "build/src/src/plugins/${f}" ] && cp -f "build/src/src/plugins/${f}" "${OUT_DIR}/plugins/"
done

# Python plugins (simple single-file)
for pyfile in \
    "libs/form43_checker/src/Form43Checker.py" \
    "libs/script_extender_plugin_checker/src/ScriptExtenderPluginChecker.py" \
    "libs/preview_dds/src/DDSPreview.py" \
    "src/plugins/installer_omod.py"; do
    [ -f "${pyfile}" ] && cp -f "${pyfile}" "${OUT_DIR}/plugins/"
done

# basic_games Python module (directory package) — copy the whole tree, .py files only
if [ -d "libs/basic_games" ]; then
    cp -a "libs/basic_games" "${OUT_DIR}/plugins/basic_games"
    # Remove non-Python clutter (metadata, lock files, vcpkg, etc.)
    find "${OUT_DIR}/plugins/basic_games" \
        \( -name "*.toml" -o -name "*.lock" -o -name "*.json" \
        -o -name "*.txt" -o -name "*.md" -o -name "LICENSE" \
        -o -name "CMakeLists.txt" -o -name "CMakePresets.json" \) \
        -delete 2>/dev/null || true
    # Remove __pycache__ directories to prevent stale bytecode
    find "${OUT_DIR}/plugins/basic_games" -type d -name "__pycache__" -exec rm -rf {} + 2>/dev/null || true
fi
# data/ dir (DDS headers etc., used by DDSPreview.py via plugins/data/ in sys.path).
# Stage DDS package from source directly (cmake staging is OFF by default).
if [ -d "libs/preview_dds/src/DDS" ]; then
    mkdir -p "${OUT_DIR}/plugins/data"
    cp -a "libs/preview_dds/src/DDS" "${OUT_DIR}/plugins/data/DDS"
fi
[ -d "build/src/src/plugins/data" ] && cp -a "build/src/src/plugins/data" "${OUT_DIR}/plugins/"

# preview_nif shaders. ShaderManager loads them from
# IOrganizer::getPluginDataPath()/shaders/, which on Linux is
# OrganizerCore::pluginDataPath() = <basePath>/plugin_data (see
# src/src/organizercore.cpp:971). Stage accordingly.
if [ -d "libs/preview_nif/data/shaders" ]; then
    mkdir -p "${OUT_DIR}/plugin_data/shaders"
    find "libs/preview_nif/data/shaders" -type f \( -name "*.vert" -o -name "*.frag" \) \
        -exec cp -f {} "${OUT_DIR}/plugin_data/shaders/" \;
fi

# ── Stylesheets (themes) ──
if [ -d "build/src/src/stylesheets" ]; then
    cp -a "build/src/src/stylesheets" "${OUT_DIR}/"
    echo "Bundled stylesheets"
fi



# ── 7z runtime ──
SO7="build/src/src/lib/7z.so"
if [ -f "${SO7}" ]; then
    cp -f "${SO7}" "${OUT_DIR}/lib/7z.so"
fi

# ── Project-specific shared libraries ──
cp -f build/libs/uibase/src/libuibase.so "${OUT_DIR}/lib/"
cp -f build/libs/libbsarch/liblibbsarch.so "${OUT_DIR}/lib/"
cp -f build/libs/archive/src/libarchive.so "${OUT_DIR}/lib/"
cp -f build/libs/plugin_python/src/runner/librunner.so "${OUT_DIR}/lib/"
if [ -f "libs/bsa_ffi/target/release/libbsa_ffi.so" ]; then
    cp -f libs/bsa_ffi/target/release/libbsa_ffi.so "${OUT_DIR}/lib/"
fi
if [ -f "libs/steam_appinfo_ffi/target/release/libsteam_appinfo_ffi.so" ]; then
    cp -f libs/steam_appinfo_ffi/target/release/libsteam_appinfo_ffi.so "${OUT_DIR}/lib/"
fi

# Boost (version-pinned to container, won't exist on most user systems).
for boost_lib in /lib/x86_64-linux-gnu/libboost_program_options.so* \
                 /lib/x86_64-linux-gnu/libboost_thread.so*; do
    [ -f "${boost_lib}" ] && cp -Lf "${boost_lib}" "${OUT_DIR}/lib/"
done

# ── Bundle ALL shared library dependencies ──
# Collect every .so the binary and plugins link against, then bundle
# everything except core glibc/system libs (which must come from the host).
echo "Bundling shared library dependencies..."

# Libraries that MUST come from the host (glibc, GPU drivers, etc.)
SKIP_PATTERN="linux-vdso|ld-linux|libc\.so|libm\.so|libdl\.so|librt\.so|libpthread|libresolv|libnss|libgcc_s|libstdc\+\+"
# GPU/graphics drivers must be host-provided
SKIP_PATTERN="${SKIP_PATTERN}|libGL\.so|libEGL|libGLX|libGLdispatch|libdrm|libvulkan|libX11|libxcb|libwayland-client|libwayland-server|libwayland-cursor|libwayland-egl|libxkbcommon"
# libpython — user provides via system Python; do not bundle.
SKIP_PATTERN="${SKIP_PATTERN}|libpython"
# OpenSSL should come from the host so we don't pin users to a stale TLS stack.
SKIP_PATTERN="${SKIP_PATTERN}|libssl\.so|libcrypto\.so"

collect_deps() {
    ldd "$1" 2>/dev/null | grep "=>" | awk '{print $3}' | grep "^/" | sort -u
}

ALL_DEPS=$(mktemp)
# Main binary
collect_deps "${OUT_DIR}/ModOrganizer-core" >> "${ALL_DEPS}"
# All plugin .so files
find "${OUT_DIR}/plugins" -name "*.so" -exec sh -c 'ldd "$1" 2>/dev/null | grep "=>" | awk "{print \$3}" | grep "^/"' _ {} \; >> "${ALL_DEPS}"
# Our own libs
find "${OUT_DIR}/lib" -name "*.so*" -exec sh -c 'ldd "$1" 2>/dev/null | grep "=>" | awk "{print \$3}" | grep "^/"' _ {} \; >> "${ALL_DEPS}"
sort -u "${ALL_DEPS}" | while read -r dep; do
    dep_name="$(basename "${dep}")"
    # Skip system libs
    if echo "${dep_name}" | grep -qE "${SKIP_PATTERN}"; then
        continue
    fi
    # Skip if already bundled
    if [ -f "${OUT_DIR}/lib/${dep_name}" ]; then
        continue
    fi
    cp -Lf "${dep}" "${OUT_DIR}/lib/" 2>/dev/null || true
done
rm -f "${ALL_DEPS}"
echo "Dependencies bundled."

# libxcb-cursor is required by Qt's xcb platform plugin (Qt >= 6.5.0) but is
# frequently absent on user systems (package: xcb-cursor0 / libxcb-cursor0).
# All other libxcb libs are skipped above because they must match the host X
# server ABI; libxcb-cursor is a pure utility library with no ABI dependency
# on the X server version, so it's safe to bundle.
for _xcb_cursor in /lib/x86_64-linux-gnu/libxcb-cursor.so* \
                   /usr/lib/x86_64-linux-gnu/libxcb-cursor.so*; do
    [ -f "${_xcb_cursor}" ] && cp -Lf "${_xcb_cursor}" "${OUT_DIR}/lib/" && \
        echo "Bundled ${_xcb_cursor}"
done

# xdg-mime's KDE path calls an unversioned `qtpaths` helper. Bundle it so
# users do not need distro Qt tools packages just to register nxm:// links.
QT6_BIN_DIR=""
for _candidate in \
    "${Qt6_DIR:-}/bin" \
    "/opt/qt6/6.11.1/gcc_64/bin" \
    "/usr/lib/qt6/bin"; do
    if [ -d "${_candidate}" ]; then
        QT6_BIN_DIR="${_candidate}"
        break
    fi
done
for _qtpaths in \
    "${QT6_BIN_DIR}/qtpaths" \
    "${QT6_BIN_DIR}/qtpaths6" \
    "$(command -v qtpaths 2>/dev/null || true)" \
    "$(command -v qtpaths6 2>/dev/null || true)"; do
    if [ -n "${_qtpaths}" ] && [ -x "${_qtpaths}" ]; then
        cp -Lf "${_qtpaths}" "${OUT_DIR}/lib/qtpaths"
        chmod +x "${OUT_DIR}/lib/qtpaths"
        echo "Bundled qtpaths from ${_qtpaths}"
        break
    fi
done

# ── Qt6 platform plugins ──
# Prefer aqtinstall location (Docker), then system, then qtpaths6 fallback.
QT6_PLUGIN_DIR=""
for _candidate in \
    "${Qt6_DIR:-}/plugins" \
    "/opt/qt6/6.11.1/gcc_64/plugins" \
    "/usr/lib/x86_64-linux-gnu/qt6/plugins"; do
    if [ -d "${_candidate}" ]; then
        QT6_PLUGIN_DIR="${_candidate}"
        break
    fi
done
if [ -z "${QT6_PLUGIN_DIR}" ]; then
    QT6_PLUGIN_DIR="$(qtpaths6 --plugin-dir 2>/dev/null || echo "")"
fi
if [ -d "${QT6_PLUGIN_DIR}" ]; then
    mkdir -p "${OUT_DIR}/qt6plugins"
    for plugin_type in platforms tls networkinformation styles \
                       wayland-shell-integration \
                       wayland-decoration-client wayland-graphics-integration-client \
                       platformthemes imageformats iconengines xcbglintegrations \
                       egldeviceintegrations; do
        if [ -d "${QT6_PLUGIN_DIR}/${plugin_type}" ]; then
            cp -a "${QT6_PLUGIN_DIR}/${plugin_type}" "${OUT_DIR}/qt6plugins/"
        fi
    done
    # Bundle deps of Qt plugins too
    find "${OUT_DIR}/qt6plugins" -name "*.so" -exec sh -c '
        ldd "$1" 2>/dev/null | grep "=>" | awk "{print \$3}" | grep "^/" | while read dep; do
            dep_name="$(basename "${dep}")"
            echo "${dep_name}" | grep -qE "'"${SKIP_PATTERN}"'" && continue
            [ -f "'"${OUT_DIR}"'/lib/${dep_name}" ] && continue
            cp -Lf "${dep}" "'"${OUT_DIR}"'/lib/" 2>/dev/null || true
        done
    ' _ {} \;
    echo "Bundled Qt6 plugins from ${QT6_PLUGIN_DIR}"
else
    echo "WARNING: Could not find Qt6 plugin directory"
fi

# ── Bundle PBS Python 3.12 runtime ──
# PYTHONHOME only needs lib/python3.12/ (the stdlib). We do NOT copy the
# binary, headers, static lib, or .py sources — only stripped .pyc + .so.
PBS_SRC="/opt/python-bundled"
PYTHON_OUT="${OUT_DIR}/python"
mkdir -p "${PYTHON_OUT}/lib"

# Copy only the stdlib directory
cp -a "${PBS_SRC}/lib/python3.12" "${PYTHON_OUT}/lib/"

# Remove only what is safe — test suites, GUI toolkits, dev tools.
# Do NOT strip network/stdlib modules; basic_games uses email, http, xml, urllib, etc.
find "${PYTHON_OUT}" -type d \( -name "test" -o -name "tests" \) \
    -exec rm -rf {} + 2>/dev/null || true
rm -rf "${PYTHON_OUT}/lib/python3.12/tkinter"
rm -rf "${PYTHON_OUT}/lib/python3.12/ensurepip"
rm -rf "${PYTHON_OUT}/lib/python3.12/distutils"
rm -rf "${PYTHON_OUT}/lib/python3.12/lib2to3"
rm -rf "${PYTHON_OUT}/lib/python3.12/idlelib"
rm -rf "${PYTHON_OUT}/lib/python3.12/turtledemo"
rm -f  "${PYTHON_OUT}/lib/python3.12/turtle.py"
# Wipe site-packages entirely — build-time packages (pybind11, PyQt6, sip, etc.)
# are not needed at runtime. PyQt6 is staged separately to plugins/libs/PyQt6/.
rm -rf "${PYTHON_OUT}/lib/python3.12/site-packages"
mkdir -p "${PYTHON_OUT}/lib/python3.12/site-packages"
# Copy runtime-required packages back in
for pkg in psutil vdf larian_formats; do
    pkg_dir="$("${PBS_SRC}/bin/python3" -c "import importlib.util; s=importlib.util.find_spec('${pkg}'); print(s.submodule_search_locations[0] if s and s.submodule_search_locations else (s.origin if s else ''))" 2>/dev/null || true)"
    if [ -d "${pkg_dir}" ]; then
        cp -a "${pkg_dir}" "${PYTHON_OUT}/lib/python3.12/site-packages/"
    elif [ -f "${pkg_dir}" ]; then
        cp -f "${pkg_dir}" "${PYTHON_OUT}/lib/python3.12/site-packages/"
    fi
done

# Pre-compile .py → .pyc (PBS ships .py + .pyc; this ensures cache is fresh).
# We keep the .py source files — Python's SourceFileLoader requires them to
# find the corresponding __pycache__/*.pyc files. Deleting them breaks imports.
"${PBS_SRC}/bin/python3" -m compileall -q "${PYTHON_OUT}/lib/python3.12/" 2>/dev/null || true

# Strip debug info from extension modules
find "${PYTHON_OUT}/lib/python3.12" -name "*.so" \
    -exec strip --strip-unneeded {} \; 2>/dev/null || true

# libpython shared library goes in our lib/ (dlopen'd by librunner.so via $ORIGIN RPATH)
# Not placed inside python/ — PYTHONHOME doesn't need the shared lib alongside the stdlib.
cp -Lf "${PBS_SRC}/lib/libpython3.12.so.1.0" "${OUT_DIR}/lib/"
strip --strip-unneeded "${OUT_DIR}/lib/libpython3.12.so.1.0" 2>/dev/null || true
ln -sf libpython3.12.so.1.0 "${OUT_DIR}/lib/libpython3.12.so"
echo "Bundled PBS Python 3.12: $(du -sh "${PYTHON_OUT}" | cut -f1)"

# ── Bundle PyQt6 (bindings only — reuse our bundled Qt, no duplicate Qt .so) ──
# PyQt6 pip wheel bundles Qt under PyQt6/Qt6/lib/ which we strip out.
# The binding .so files are patchelf'd to find our Qt in lib/.
PYQT6_SRC="$("${PBS_SRC}/bin/python3" -c 'import PyQt6, os; print(os.path.dirname(PyQt6.__file__))')"
PYQT6_OUT="${OUT_DIR}/plugins/libs/PyQt6"
mkdir -p "${PYQT6_OUT}"
cp -a "${PYQT6_SRC}/." "${PYQT6_OUT}/"
# Remove PyQt6's bundled Qt — we already have Qt in lib/
rm -rf "${PYQT6_OUT}/Qt6"

# Prune unused PyQt6 modules. Full-repo grep of libs/ + src/ confirms shipped
# Python plugins only import the five modules below. Pruning happens BEFORE
# the deps-scan loop further down so libQt6Designer/Help/Sql/Test/SvgWidgets/
# Bluetooth/... stop getting copied into lib/ as a side effect.
PYQT6_KEEP_MODULES="QtCore QtGui QtWidgets QtOpenGL QtOpenGLWidgets"
rm -rf "${PYQT6_OUT}/bindings" "${PYQT6_OUT}/uic" "${PYQT6_OUT}/lupdate"
find "${PYQT6_OUT}" -type d -name __pycache__ -exec rm -rf {} + 2>/dev/null || true
find "${PYQT6_OUT}" -type f -name '*.pyi' -delete 2>/dev/null || true
for entry in "${PYQT6_OUT}"/*; do
    [ -e "${entry}" ] || continue
    base="$(basename "${entry}")"
    case "${base}" in
        __init__.py|py.typed)   continue ;;
        sip.cpython-*.so)       continue ;;
        Qt6)                    continue ;;  # already removed; defensive
        *.abi3.so)
            mod="${base%.abi3.so}"
            keep=0
            for k in ${PYQT6_KEEP_MODULES}; do
                [ "${mod}" = "${k}" ] && keep=1 && break
            done
            [ ${keep} -eq 0 ] && rm -f "${entry}"
            ;;
        *)
            rm -rf "${entry}"
            ;;
    esac
done
echo "PyQt6 pruned to: ${PYQT6_KEEP_MODULES}"

# Patchelf all PyQt6 binding .so files to reach our lib/ via RPATH
# Path: plugins/libs/PyQt6/*.so → ../../.. = staging root → lib/
find "${PYQT6_OUT}" -name "*.so" -exec \
    patchelf --force-rpath --set-rpath '$ORIGIN/../../../lib' {} \; 2>/dev/null || true
strip --strip-unneeded "${PYQT6_OUT}"/*.so 2>/dev/null || true
echo "Bundled PyQt6 (no Qt dupe): $(du -sh "${PYQT6_OUT}" | cut -f1)"

# Scan PyQt6 binding deps and bundle any Qt libs not yet in lib/.
# PyQt6 is bundled after the main dep-collection loop, so its deps (e.g.
# libQt6OpenGLWidgets) would otherwise be missing. Without them the dynamic
# linker falls back to the host's Qt RPM build, which uses Qt_6.*_PRIVATE_API
# version symbols that aqtinstall's Qt libs don't export — causing crashes on
# distros like Bazzite/Fedora that ship their own Qt.
echo "Scanning PyQt6 deps for missing Qt libs..."
find "${PYQT6_OUT}" -name "*.so" | while read -r pyqt_so; do
    ldd "${pyqt_so}" 2>/dev/null | grep "=>" | awk '{print $3}' | grep "^/" | while read -r dep; do
        dep_name="$(basename "${dep}")"
        if echo "${dep_name}" | grep -qE "${SKIP_PATTERN}"; then continue; fi
        if [ -f "${OUT_DIR}/lib/${dep_name}" ]; then continue; fi
        cp -Lf "${dep}" "${OUT_DIR}/lib/" 2>/dev/null && echo "  + ${dep_name}" || true
    done
done

# ── Dedupe identical files in lib/ via symlinks ──
# Source-side `cp -Lf` resolves symlinks, so unversioned (foo.so) and versioned
# (foo.so.X.Y.Z) get staged as identical real files. Replace the unversioned
# (or shorter-versioned) twin with a symlink to the longest real file.
# MUST run after both the main dep-collection loop and the PyQt6 ldd scan
# above; do not reorder.
echo "Deduping lib/ via symlinks..."
(
    cd "${OUT_DIR}/lib" || exit 0
    # Pass A: foo.so → foo.so.X[.Y[.Z]]
    for f in *.so; do
        [ -L "${f}" ] && continue
        [ -f "${f}" ] || continue
        target=""
        for v in $(ls -1 "${f}".* 2>/dev/null | sort -r); do
            [ -L "${v}" ] && continue
            [ -f "${v}" ] || continue
            if cmp -s "${f}" "${v}"; then
                target="${v}"
                break
            fi
        done
        if [ -n "${target}" ]; then
            rm -f "${f}"
            ln -s "${target}" "${f}"
            echo "  link ${f} -> ${target}"
        fi
    done
    # Pass B: foo.so.N → foo.so.N.M[.P] (handles e.g. libxcb-cursor.so.0 → .so.0.0.0)
    for f in *.so.[0-9]*; do
        [ -L "${f}" ] && continue
        [ -f "${f}" ] || continue
        target=""
        for v in $(ls -1 "${f}".* 2>/dev/null | sort -r); do
            [ -L "${v}" ] && continue
            [ -f "${v}" ] || continue
            if cmp -s "${f}" "${v}"; then
                target="${v}"
                break
            fi
        done
        if [ -n "${target}" ]; then
            rm -f "${f}"
            ln -s "${target}" "${f}"
            echo "  link ${f} -> ${target}"
        fi
    done
)

# ── Strip all MO2 binaries ──
echo "Stripping MO2 binaries..."
strip --strip-unneeded "${OUT_DIR}/ModOrganizer-core" 2>/dev/null || true
find "${OUT_DIR}/plugins" -name "*.so" -exec strip --strip-unneeded {} \; 2>/dev/null || true
find "${OUT_DIR}/lib" -name "*.so" -exec strip --strip-unneeded {} \; 2>/dev/null || true

# ── Fix RPATH so binaries find libs without LD_LIBRARY_PATH ──
# Use --force-rpath to set DT_RPATH (not DT_RUNPATH) for reliable
# library resolution regardless of LD_LIBRARY_PATH.
echo "Patching RPATH..."
patchelf --force-rpath --set-rpath '$ORIGIN/lib' "${OUT_DIR}/ModOrganizer-core"
find "${OUT_DIR}/plugins" -maxdepth 1 -name "*.so" -exec patchelf --force-rpath --set-rpath '$ORIGIN/../lib' {} \; 2>/dev/null || true
find "${OUT_DIR}/plugins/libs" -name "*.so" -exec patchelf --force-rpath --set-rpath '$ORIGIN/../../lib' {} \; 2>/dev/null || true
find "${OUT_DIR}/lib" \( -name "*.so" -o -name "*.so.*" \) -exec patchelf --force-rpath --set-rpath '$ORIGIN' {} \; 2>/dev/null || true
# Qt platform plugins keep aqtinstall's hardcoded RPATH (/opt/qt6/.../lib) which
# doesn't exist on user systems — the linker falls through to system Qt, loading
# the wrong version and poisoning the link map for all subsequent Qt library
# lookups (including PyQt6 bindings).  All Qt plugins sit one subdir deep under
# qt6plugins/, so $ORIGIN/../../lib resolves correctly to our lib/ for all of them.
find "${OUT_DIR}/qt6plugins" -name "*.so" -exec patchelf --force-rpath --set-rpath '$ORIGIN/../../lib' {} \; 2>/dev/null || true

# ── Launcher script ──
cat > "${OUT_DIR}/fluorine-manager" <<'LAUNCH'
#!/usr/bin/env bash
set -euo pipefail
SELF="$(readlink -f "$0")"
HERE="$(cd "$(dirname "$SELF")" && pwd)"

# Save the original environment so game launches (Proton/Wine) can restore it.
# Without this, our bundled LD_LIBRARY_PATH leaks into game processes and
# causes library conflicts.
export FLUORINE_ORIG_LD_LIBRARY_PATH="${LD_LIBRARY_PATH:-}"
export FLUORINE_ORIG_LD_PRELOAD="${LD_PRELOAD:-}"
export FLUORINE_ORIG_PATH="${PATH}"
export FLUORINE_ORIG_XDG_DATA_DIRS="${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"
export FLUORINE_ORIG_QT_PLUGIN_PATH="${QT_PLUGIN_PATH:-}"

# Steam injects 32-bit gameoverlayrenderer.so via LD_PRELOAD which causes
# "wrong ELF class" errors for 64-bit Qt6 apps (see PrismLauncher #3421).
# Clear it for our process; game launches restore via FLUORINE_ORIG_LD_PRELOAD.
unset LD_PRELOAD

# Suppress Qt debug logging by default. Some plugins (e.g. BG3 file mapper)
# qDebug() Path objects whose surrogate-escaped bytes crash Qt's logger with
# "unicode category" errors. User can re-enable with QT_LOGGING_RULES override.
: "${QT_LOGGING_RULES:=default.debug=false}"
export QT_LOGGING_RULES

# ── Sync entire app to ~/.local/share/fluorine/bin/ ──
# This gives instances a stable symlink target that won't break if the user
# moves or deletes the original tarball extraction directory.
FLUORINE_DATA="${HOME}/.local/share/fluorine"
BIN_DST="${FLUORINE_DATA}/bin"

# Guard: if we ARE already running from the installed location, skip the sync.
# Without this, running the binary directly from BIN_DST would rm -rf itself.
HERE_REAL="$(readlink -f "${HERE}")"
DST_REAL="$(readlink -f "${BIN_DST}" 2>/dev/null || echo "")"
if [ "${HERE_REAL}" != "${DST_REAL}" ]; then
    # Manifest lists top-level entries that belong to Fluorine. Without it we
    # can't distinguish our files from whatever else the user parked next to
    # the launcher (e.g. extracted into ~/Downloads alongside their mods).
    MANIFEST="${HERE}/fluorine-manifest.txt"
    if [ ! -f "${MANIFEST}" ] || [ ! -f "${HERE}/ModOrganizer-core" ]; then
        echo "ERROR: Fluorine launcher can't find its bundle files in ${HERE}." >&2
        echo "Extract the release archive into its own directory and run the" >&2
        echo "launcher from there — not from a folder containing other files." >&2
        exit 1
    fi

    # Fingerprint from size+mtime (not inode) so re-extracting the same tarball
    # onto a different filesystem doesn't trigger a spurious full resync.
    CURRENT_VER="$(stat -c '%s:%Y' "${HERE}/ModOrganizer-core" 2>/dev/null || echo "unknown")"
    MARKER="${BIN_DST}/.version"

    if [ ! -f "${MARKER}" ] || [ "$(cat "${MARKER}" 2>/dev/null)" != "${CURRENT_VER}" ]; then
        if [ -d "${BIN_DST}" ]; then
            echo "Updating Fluorine in ${BIN_DST}..." >&2
        else
            echo "Installing Fluorine to ${BIN_DST} (this may take a minute on first launch)..." >&2
        fi
        mkdir -p "${BIN_DST}"

        # Overlay update — preserves anything the user dropped into bin/
        # (custom plugins under plugins/, a portable instance with
        # ModOrganizer.ini, downloaded mods, etc.). The manifest is our
        # authoritative list of "ours"; everything else is left alone.
        #
        # Step 1: orphan removal. Top-level entries we shipped before but no
        # longer ship get removed. Top-level granularity is enough — when the
        # entry is still in both manifests (e.g. "plugins"), we don't touch
        # the directory itself, only overwrite our own files inside.
        OLD_MANIFEST="${BIN_DST}/fluorine-manifest.txt"
        if [ -f "${OLD_MANIFEST}" ]; then
            # comm -23 = lines unique to the OLD manifest. sort -u for comm.
            while IFS= read -r entry; do
                [ -z "${entry}" ] && continue
                # Refuse anything that could escape BIN_DST.
                case "${entry}" in /*|*..*|.|..) continue ;; esac
                rm -rf "${BIN_DST:?}/${entry}"
            done < <(comm -23 \
                <(sort -u "${OLD_MANIFEST}") \
                <(sort -u "${MANIFEST}"))
        fi

        # Step 2: overlay copy. tar piped to tar streams the manifested entries
        # from HERE into BIN_DST. Existing directories stay; existing files we
        # ship get overwritten with the new version; user-added files inside
        # our directories (e.g. plugins/MyCustomPlugin.so) are preserved.
        if ! tar -C "${HERE}" -cf - --files-from="${MANIFEST}" 2>/dev/null \
             | tar -C "${BIN_DST}" --no-same-owner -xf - ; then
            echo "ERROR: Fluorine sync failed mid-copy. The install may be in a broken state." >&2
            echo "Re-extract the release archive and run the launcher again." >&2
            exit 1
        fi

        # Do not retain stale bundled OpenSSL runtimes from older releases.
        # TLS should resolve against the host so certificate handling can be
        # updated by the OS instead of being pinned to our old package.
        rm -f "${BIN_DST}"/lib/libssl.so* "${BIN_DST}"/lib/libcrypto.so* 2>/dev/null || true

        # Refresh the manifest at the destination so the next update has it.
        cp -af "${MANIFEST}" "${BIN_DST}/fluorine-manifest.txt"
        echo "${CURRENT_VER}" > "${MARKER}"
        echo "Sync complete." >&2

        # Clean up the in-app updater's staging directory once the new bundle
        # is live. Untouched if the user didn't go through the in-app updater.
        STAGING_DIR="${FLUORINE_DATA}/update-staging"
        [ -d "${STAGING_DIR}" ] && rm -rf "${STAGING_DIR}" 2>/dev/null || true
    fi
fi

# ── Install icon + desktop file for Wayland taskbar/decoration ──
ICON_SRC="${BIN_DST}/icons/com.fluorine.manager.png"
ICON_DST="${HOME}/.local/share/icons/hicolor/256x256/apps/com.fluorine.manager.png"
DESKTOP_SRC="${BIN_DST}/icons/com.fluorine.manager.desktop"
DESKTOP_DST="${HOME}/.local/share/applications/com.fluorine.manager.desktop"
if [ -f "${ICON_SRC}" ] && [ ! -f "${ICON_DST}" ]; then
    mkdir -p "$(dirname "${ICON_DST}")"
    cp -f "${ICON_SRC}" "${ICON_DST}"
fi
if [ -f "${DESKTOP_SRC}" ] && [ ! -f "${DESKTOP_DST}" ]; then
    mkdir -p "$(dirname "${DESKTOP_DST}")"
    sed "s|^Exec=fluorine-manager|Exec=${BIN_DST}/fluorine-manager|" "${DESKTOP_SRC}" > "${DESKTOP_DST}"
    chmod +x "${DESKTOP_DST}"
fi

# Run from the synced location.
RUN="${BIN_DST}"

export PATH="${RUN}/lib:${RUN}:${PATH}"
# Steam game mode injects its scout/soldier runtime into LD_LIBRARY_PATH.
# Those old libraries (libssl, libz, etc.) break Python extension modules
# and Qt internals that don't have RPATH pointing to our bundled libs.
# Clear it and set only our lib/ — the binary uses DT_RPATH ($ORIGIN/lib)
# for its own deps, this covers dlopen'd plugins.
export LD_LIBRARY_PATH="${RUN}/lib"
export MO2_BASE_DIR="${RUN}"
export MO2_PLUGINS_DIR="${RUN}/plugins"
export MO2_LIBS_DIR="${RUN}/lib"
unset PYTHONPATH PYTHONNOUSERSITE PYTHONHOME MO2_PYTHON_DIR

# Use bundled Qt6 plugins.
# Set both vars: QT_QPA_PLATFORM_PLUGIN_PATH is highest priority for platform
# plugin lookup and overrides system-wide qt.conf (e.g. Fedora's /etc/xdg/QtProject/).
export QT_PLUGIN_PATH="${RUN}/qt6plugins"
export QT_QPA_PLATFORM_PLUGIN_PATH="${RUN}/qt6plugins/platforms"

# Raise open file descriptor limit — large modlists with FUSE VFS
# can easily exceed the default 1024
ulimit -n 65536 2>/dev/null

cd "${RUN}"
exec "${RUN}/ModOrganizer-core" "$@"
LAUNCH
chmod +x "${OUT_DIR}/fluorine-manager"

# ── qt.conf — tells Qt where to find plugins without QT_PLUGIN_PATH env ──
cat > "${OUT_DIR}/qt.conf" <<'QTCONF'
[Paths]
Plugins = qt6plugins
QTCONF

# ── Desktop integration files ──
mkdir -p "${OUT_DIR}/icons"
cp -f /src/data/icons/com.fluorine.manager.desktop "${OUT_DIR}/icons/"
cp -f /src/data/icons/com.fluorine.manager.png "${OUT_DIR}/icons/"
cp -f /src/data/icons/com.fluorine.manager.metainfo.xml "${OUT_DIR}/icons/"

# ── Manifest of top-level entries ──
# The launcher's sync step copies only files listed here into
# ~/.local/share/fluorine/bin/. Without a manifest it would have to guess
# (previously: tar the whole extraction dir), and would slurp any unrelated
# files the user parked next to the launcher — e.g. 38 GB of mods in Downloads.
(cd "${OUT_DIR}" && ls -A | grep -v '^fluorine-manifest\.txt$') > "${OUT_DIR}/fluorine-manifest.txt"
echo "Wrote manifest: $(wc -l < "${OUT_DIR}/fluorine-manifest.txt") entries"

# ── Determine build mode ──
# BUILD_MODE is passed from build.sh: tarball (default), installer, all
BUILD_MODE="${BUILD_MODE:-tarball}"

# ── Build portable distribution (directory) ──
# No archive created — GitHub zips release assets automatically.
build_tarball() {
    echo ""
    echo "=== Building portable distribution ==="
    cd /src/build
    TARBALL_NAME="fluorine-manager"
    rm -rf "${TARBALL_NAME}"
    cp -a staging "${TARBALL_NAME}"
    echo "Output: /src/build/${TARBALL_NAME}/"
    du -sh "/src/build/${TARBALL_NAME}"
}

# ── Build self-extracting installer (.bin frontloader) ──
build_installer() {
    echo ""
    echo "=== Building installer ==="

    # Create the installer header script
    INSTALLER_SCRIPT="/src/build/installer-header.sh"
    cat > "${INSTALLER_SCRIPT}" <<'INSTALLER_HEADER'
#!/usr/bin/env bash
set -euo pipefail

APP_NAME="Fluorine Manager"
INSTALL_DIR="${HOME}/.local/share/fluorine/bin"
DESKTOP_DIR="${HOME}/.local/share/applications"
ICON_DIR="${HOME}/.local/share/icons/hicolor/256x256/apps"

echo ""
echo "╔══════════════════════════════════════════╗"
echo "║        Fluorine Manager Installer        ║"
echo "╚══════════════════════════════════════════╝"
echo ""

# Detect existing installation
if [ -d "${INSTALL_DIR}" ] && [ -f "${INSTALL_DIR}/ModOrganizer-core" ]; then
    echo "Existing installation detected at: ${INSTALL_DIR}"
    echo ""
    echo "  1) Update existing installation"
    echo "  2) Extract portable copy here (./fluorine-manager/)"
    echo "  3) Cancel"
    echo ""
    read -rp "Choose [1/2/3]: " CHOICE
else
    echo "  1) Install to ${INSTALL_DIR} (global)"
    echo "  2) Extract portable copy here (./fluorine-manager/)"
    echo "  3) Cancel"
    echo ""
    read -rp "Choose [1/2/3]: " CHOICE
fi

case "${CHOICE}" in
    1)
        echo ""
        echo "Installing to ${INSTALL_DIR}..."
        mkdir -p "${INSTALL_DIR}"
        # Extract payload (everything after the __PAYLOAD__ marker)
        ARCHIVE_START=$(awk '/^__PAYLOAD__$/{print NR + 1; exit 0;}' "$0")
        tail -n +"${ARCHIVE_START}" "$0" | tar xzf - -C "${INSTALL_DIR}" --strip-components=1

        # Create desktop shortcut
        mkdir -p "${DESKTOP_DIR}" "${ICON_DIR}"
        cp -f "${INSTALL_DIR}/icons/com.fluorine.manager.png" "${ICON_DIR}/"

        cat > "${DESKTOP_DIR}/com.fluorine.manager.desktop" <<DESKTOP_EOF
[Desktop Entry]
Type=Application
Name=Fluorine Manager
Comment=Mod Organizer for Linux
Exec=${INSTALL_DIR}/fluorine-manager %u
Icon=com.fluorine.manager
Terminal=false
Categories=Game;Utility;
StartupWMClass=ModOrganizer
DESKTOP_EOF
        chmod +x "${DESKTOP_DIR}/com.fluorine.manager.desktop"

        # Update desktop database if available
        command -v update-desktop-database >/dev/null 2>&1 && \
            update-desktop-database "${DESKTOP_DIR}" 2>/dev/null || true

        echo ""
        echo "Installation complete!"
        echo "  Binary:   ${INSTALL_DIR}/fluorine-manager"
        echo "  Shortcut: ${DESKTOP_DIR}/com.fluorine.manager.desktop"
        echo ""
        read -rp "Launch now? [Y/n]: " LAUNCH
        if [ "${LAUNCH,,}" != "n" ]; then
            exec "${INSTALL_DIR}/fluorine-manager" "$@"
        fi
        ;;
    2)
        echo ""
        PORTABLE_DIR="$(pwd)/fluorine-manager"
        echo "Extracting portable copy to ${PORTABLE_DIR}..."
        mkdir -p "${PORTABLE_DIR}"
        ARCHIVE_START=$(awk '/^__PAYLOAD__$/{print NR + 1; exit 0;}' "$0")
        tail -n +"${ARCHIVE_START}" "$0" | tar xzf - -C "${PORTABLE_DIR}" --strip-components=1

        echo ""
        echo "Portable extraction complete!"
        echo "  Run: ${PORTABLE_DIR}/fluorine-manager"
        ;;
    *)
        echo "Cancelled."
        exit 0
        ;;
esac
exit 0
__PAYLOAD__
INSTALLER_HEADER

    # Build the tarball payload
    cd /src/build
    TARBALL_NAME="fluorine-manager"
    rm -rf "${TARBALL_NAME}"
    cp -a staging "${TARBALL_NAME}"
    tar czf "${TARBALL_NAME}-payload.tar.gz" "${TARBALL_NAME}"/
    rm -rf "${TARBALL_NAME}"

    # Combine header + payload into self-extracting .bin
    cat "${INSTALLER_SCRIPT}" "${TARBALL_NAME}-payload.tar.gz" > "${TARBALL_NAME}.bin"
    chmod +x "${TARBALL_NAME}.bin"
    rm -f "${INSTALLER_SCRIPT}" "${TARBALL_NAME}-payload.tar.gz"

    echo "Installer: /src/build/${TARBALL_NAME}.bin"
    ls -lh "/src/build/${TARBALL_NAME}.bin"
}

# ── Execute requested build mode ──
case "${BUILD_MODE}" in
    tarball)
        build_tarball
        ;;
    installer)
        build_installer
        ;;
    all)
        build_tarball
        build_installer
        ;;
    *)
        echo "ERROR: Unknown BUILD_MODE '${BUILD_MODE}'. Use: tarball, installer, all"
        exit 1
        ;;
esac

echo ""
echo "=== Build Summary ==="
du -sh "${OUT_DIR}"/*/ "${OUT_DIR}"/ModOrganizer-core 2>/dev/null | sort -rh
echo ""
echo "Build outputs:"
ls -dh /src/build/fluorine-manager/ /src/build/fluorine-manager.bin 2>/dev/null || echo "  (none found)"

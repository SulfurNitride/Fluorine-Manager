#!/usr/bin/env bash
set -euo pipefail

# ── Build ──
cmake -S . -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DPython3_EXECUTABLE="$(command -v python3)" \
    -DBUILD_PLUGIN_PYTHON=ON

cmake --build build --parallel

MODORG_BIN="build/src/src/ModOrganizer"
if [ ! -f "${MODORG_BIN}" ]; then
    echo "ERROR: ModOrganizer binary not found at ${MODORG_BIN}"
    exit 1
fi
RUNDIR="build/src/src"

PY_MM="$(python3 -c 'import sys; print(f"{sys.version_info.major}.{sys.version_info.minor}")')"

# ── Output layout (staging area — installed to ~/.local/share/fluorine by build-native.sh) ──
OUT_DIR="/src/build/staging"
rm -rf "${OUT_DIR}"
mkdir -p "${OUT_DIR}/plugins" "${OUT_DIR}/dlls" "${OUT_DIR}/lib"

# ── Main binary + helpers ──
cp -f "${RUNDIR}/ModOrganizer" "${OUT_DIR}/ModOrganizer-core"
if [ -f "${RUNDIR}/umu-run" ]; then
    # Patch umu-run to fix two issues with Steamworks DRM:
    #
    # 1. Preserve STEAM_COMPAT_CLIENT_INSTALL_PATH from the parent environment.
    #    Upstream umu-run initialises this to "" and never picks up the caller's
    #    value, which prevents the Steam client libraries from being found.
    #
    # 2. Remove UMU_ID from the environment before Proton runs.  GE-Proton
    #    treats games with UMU_ID as non-Steam titles and skips the steam.exe
    #    DRM bridge, causing "Application load error 5:0000065434".  For actual
    #    Steam games (SteamAppId != "0") we delete UMU_ID so Proton uses the
    #    correct steam.exe launch path.
    UMU_PATCH_DIR="$(mktemp -d)"
    (cd "${UMU_PATCH_DIR}" && python3 << PATCHEOF
import zipfile, pathlib
zf = zipfile.ZipFile('/src/${RUNDIR}/umu-run')
zf.extractall('src')
run_py = pathlib.Path('src/umu/umu_run.py')
src = run_py.read_text()

# Patch 1: preserve STEAM_COMPAT_CLIENT_INSTALL_PATH
old1 = '    env["STEAM_COMPAT_INSTALL_PATH"] = os.environ.get("STEAM_COMPAT_INSTALL_PATH", "")'
new1 = (old1
    + '\n    env["STEAM_COMPAT_CLIENT_INSTALL_PATH"] = os.environ.get('
    + '\n        "STEAM_COMPAT_CLIENT_INSTALL_PATH", ""'
    + '\n    )')
if old1 in src and 'STEAM_COMPAT_CLIENT_INSTALL_PATH"] = os.environ' not in src:
    src = src.replace(old1, new1)

# Patch 2: delete UMU_ID before Proton launch for Steam games
old2 = '            os.environ[key] = val'
new2 = (old2
    + '\n\n        # GE-Proton treats games with UMU_ID as non-Steam titles and skips'
    + '\n        # the steam.exe DRM bridge.  Remove it for Steam games.'
    + '\n        if "UMU_ID" in os.environ and env.get("SteamAppId", "0") != "0":'
    + '\n            del os.environ["UMU_ID"]')
if old2 in src and 'del os.environ["UMU_ID"]' not in src:
    src = src.replace(old2, new2, 1)

run_py.write_text(src)
PATCHEOF
)
    python3 -m zipapp "${UMU_PATCH_DIR}/src" -o "${OUT_DIR}/umu-run" -p '/usr/bin/env python3'
    chmod +x "${OUT_DIR}/umu-run"
    rm -rf "${UMU_PATCH_DIR}"
fi
[ -f "${RUNDIR}/README-PORTABLE.txt" ] && cp -f "${RUNDIR}/README-PORTABLE.txt" "${OUT_DIR}/"
[ -f "/src/src/fluorine-manager" ] && cp -f "/src/src/fluorine-manager" "${OUT_DIR}/"

# lootcli (spawned by MO2 for load-order sorting).
LOOTCLI="build/libs/lootcli/src/lootcli"
[ -f "${LOOTCLI}" ] && cp -f "${LOOTCLI}" "${OUT_DIR}/"

for tool in wrestool icotool; do
    command -v "${tool}" >/dev/null 2>&1 && cp -f "$(command -v "${tool}")" "${OUT_DIR}/"
done

# ── MO2 plugins (.so) ──
find build/libs -type f \( \
    -name "libgame_*.so" -o \
    -name "libinstaller_*.so" -o \
    -name "libpreview_*.so" -o \
    -name "libdiagnose_*.so" -o \
    -name "libcheck_*.so" -o \
    -name "libtool_*.so" -o \
    -name "libinieditor.so" -o \
    -name "libinibakery.so" -o \
    -name "libbsa_extractor.so" -o \
    -name "libbsa_packer.so" -o \
    -name "libproxy.so" \
\) -exec cp -f {} "${OUT_DIR}/plugins/" \;

# Python plugin payload.
for f in libplugin_python.so lzokay.py winreg.py pyCfg.py \
         DDSPreview.py Form43Checker.py ScriptExtenderPluginChecker.py; do
    [ -f "build/src/src/plugins/${f}" ] && cp -f "build/src/src/plugins/${f}" "${OUT_DIR}/plugins/"
done
for d in basic_games data libs dlls; do
    [ -d "build/src/src/plugins/${d}" ] && cp -a "build/src/src/plugins/${d}" "${OUT_DIR}/plugins/"
done
rm -f "${OUT_DIR}/plugins/FNIS"*.py

# Source-tree Python plugins (OMOD installer, etc.).
for f in /src/src/plugins/*.py; do
    [ -f "${f}" ] && cp -f "${f}" "${OUT_DIR}/plugins/"
done

# ── 7z runtime ──
SO7="build/src/src/dlls/7z.so"
if [ -f "${SO7}" ]; then
    cp -f "${SO7}" "${OUT_DIR}/dlls/7z.so"
    cp -f "${SO7}" "${OUT_DIR}/dlls/7zip.dll"
fi

# ── Project-specific shared libraries ──
cp -f build/libs/uibase/src/libuibase.so "${OUT_DIR}/lib/"
cp -f build/libs/libbsarch/liblibbsarch.so "${OUT_DIR}/lib/"
cp -f build/libs/archive/src/libarchive.so "${OUT_DIR}/lib/"
cp -f build/libs/plugin_python/src/runner/librunner.so "${OUT_DIR}/lib/"
for ffi in libs/bsa_ffi/target/release/libbsa_ffi.so \
           libs/nak_ffi/target/release/libnak_ffi.so; do
    [ -f "${ffi}" ] && cp -f "${ffi}" "${OUT_DIR}/lib/"
done

# Boost (version-pinned to container, won't exist on most user systems).
for boost_lib in /lib/x86_64-linux-gnu/libboost_program_options.so* \
                 /lib/x86_64-linux-gnu/libboost_thread.so*; do
    [ -f "${boost_lib}" ] && cp -Lf "${boost_lib}" "${OUT_DIR}/lib/"
done

# libloot (custom-built, never on user systems).
if [ -f /usr/local/lib/libloot.so.0 ]; then
    cp -Lf /usr/local/lib/libloot.so.0 "${OUT_DIR}/lib/"
    # Create the unversioned symlink too.
    ln -sf libloot.so.0 "${OUT_DIR}/lib/libloot.so"
fi

# ── Portable Python runtime ──
PORTABLE_PY="/opt/portable-python"
if [ -d "${PORTABLE_PY}" ]; then
    echo "Bundling portable Python from ${PORTABLE_PY}..."
    cp -a "${PORTABLE_PY}" "${OUT_DIR}/python"

    # Trim unnecessary files from portable Python.
    PP_STDLIB="${OUT_DIR}/python/lib/python${PY_MM}"
    rm -rf "${PP_STDLIB}/test" \
           "${PP_STDLIB}/unittest/test" \
           "${PP_STDLIB}/idlelib" \
           "${PP_STDLIB}/tkinter" \
           "${PP_STDLIB}/turtledemo" \
           "${PP_STDLIB}/__pycache__" \
           "${OUT_DIR}/python/include" \
           "${OUT_DIR}/python/share" \
           2>/dev/null || true
    find "${OUT_DIR}/python" -type d -name "__pycache__" -exec rm -rf {} + 2>/dev/null || true
    find "${OUT_DIR}/python" -name "*.pyc" -delete 2>/dev/null || true

    # Ensure versioned soname symlink exists (pybind11 links against libpython3.13.so.1.0).
    if [ -f "${OUT_DIR}/python/lib/libpython${PY_MM}.so" ] && \
       [ ! -f "${OUT_DIR}/python/lib/libpython${PY_MM}.so.1.0" ]; then
        ln -sf "libpython${PY_MM}.so" "${OUT_DIR}/python/lib/libpython${PY_MM}.so.1.0"
    fi
else
    echo "ERROR: Portable Python not found at ${PORTABLE_PY}"
    exit 1
fi

# Bundle PyQt6 from system into portable Python's site-packages.
PYSITE="${OUT_DIR}/python/lib/python${PY_MM}/site-packages"
mkdir -p "${PYSITE}"
for search_dir in /usr/lib/python3/dist-packages \
                  "/usr/lib/python${PY_MM}/dist-packages" \
                  "/usr/local/lib/python${PY_MM}/dist-packages"; do
    if [ -d "${search_dir}/PyQt6" ]; then
        echo "Bundling PyQt6 from ${search_dir}..."
        cp -a "${search_dir}/PyQt6" "${PYSITE}/"
        [ -d "${search_dir}/PyQt6_sip" ] && cp -a "${search_dir}/PyQt6_sip" "${PYSITE}/"
        [ -d "${search_dir}/sip" ] && cp -a "${search_dir}/sip" "${PYSITE}/"
        break
    fi
done

# Bundle pip-installed Python packages (psutil etc.).
for search_dir in "/usr/local/lib/python${PY_MM}/dist-packages" \
                  /usr/lib/python3/dist-packages \
                  "/usr/lib/python${PY_MM}/dist-packages"; do
    for pkg in psutil vdf; do
        [ -d "${search_dir}/${pkg}" ] && [ ! -d "${PYSITE}/${pkg}" ] && \
            cp -a "${search_dir}/${pkg}" "${PYSITE}/"
    done
done

# Build-tree Python plugin payload.
[ -d build/src/src/python ] && cp -a build/src/src/python/. "${OUT_DIR}/python/"

# ── Strip all MO2 binaries (not portable Python) ──
echo "Stripping MO2 binaries..."
strip --strip-unneeded "${OUT_DIR}/ModOrganizer-core" 2>/dev/null || true
find "${OUT_DIR}/plugins" -name "*.so" -exec strip --strip-unneeded {} \; 2>/dev/null || true
find "${OUT_DIR}/dlls" -name "*.so" -o -name "*.dll" | xargs -r strip --strip-unneeded 2>/dev/null || true
find "${OUT_DIR}/lib" -name "*.so" -exec strip --strip-unneeded {} \; 2>/dev/null || true
for tool in wrestool icotool lootcli; do
    [ -f "${OUT_DIR}/${tool}" ] && strip --strip-unneeded "${OUT_DIR}/${tool}" 2>/dev/null || true
done

# ── Validate embedded Python runtime ──
cat > /tmp/mo2_embed_py_check.c <<'C'
#include <Python.h>
int main(void) {
  Py_Initialize();
  int rc = PyRun_SimpleString(
      "import zlib\n"
      "import runpy\n"
      "import zipimport\n"
      "print('python embed check ok')\n");
  if (PyErr_Occurred()) {
    PyErr_Print();
  }
  Py_Finalize();
  return rc;
}
C
gcc /tmp/mo2_embed_py_check.c -o /tmp/mo2_embed_py_check $(python3-config --embed --cflags --ldflags)
if ! PYTHONHOME="${OUT_DIR}/python" \
     PYTHONPATH="${OUT_DIR}/python/lib/python${PY_MM}:${PYSITE}" \
     LD_LIBRARY_PATH="${OUT_DIR}/lib:${OUT_DIR}/python/lib:${LD_LIBRARY_PATH:-}" \
     /tmp/mo2_embed_py_check; then
    echo "ERROR: Embedded Python runtime check failed."
    exit 1
fi

# ── Launcher script ──
cat > "${OUT_DIR}/fluorine-manager" <<'LAUNCH'
#!/usr/bin/env bash
set -euo pipefail
SELF="$(readlink -f "$0")"
HERE="$(cd "$(dirname "$SELF")" && pwd)"
export PATH="${HERE}:${PATH}"
export LD_LIBRARY_PATH="${HERE}/lib:${HERE}/python/lib:${LD_LIBRARY_PATH:-}"
export MO2_BASE_DIR="${HERE}"
export MO2_PLUGINS_DIR="${HERE}/plugins"
export MO2_DLLS_DIR="${HERE}/dlls"
export MO2_PYTHON_DIR="${HERE}/python"
# PYTHONHOME is set only for the MO2 process (not exported to children like
# umu-run/Proton which have their own Python).  MO2_PYTHON_DIR lets the
# binary reconstruct it internally.
MO2_PYTHONHOME="${HERE}/python"
unset PYTHONPATH PYTHONNOUSERSITE PYTHONHOME

# Find system Qt6 plugins (compiled-in path won't match across distros).
if [ -z "${QT_PLUGIN_PATH:-}" ]; then
    for qt_dir in /usr/lib/qt6/plugins \
                  /usr/lib/x86_64-linux-gnu/qt6/plugins \
                  /usr/lib64/qt6/plugins; do
        if [ -d "${qt_dir}/platforms" ]; then
            export QT_PLUGIN_PATH="${qt_dir}"
            break
        fi
    done
    if [ -z "${QT_PLUGIN_PATH:-}" ] && command -v qtpaths6 >/dev/null 2>&1; then
        export QT_PLUGIN_PATH="$(qtpaths6 --plugin-dir)"
    fi
fi

# Quick dependency check.
missing="$(ldd "${HERE}/ModOrganizer-core" 2>/dev/null | grep "not found" || true)"
if [ -n "${missing}" ]; then
    echo "ERROR: Missing system libraries:"
    echo "${missing}"
    echo ""
    echo "On Arch/CachyOS:  sudo pacman -S qt6-base qt6-websockets qt6-wayland"
    echo "On Fedora:        sudo dnf install qt6-qtbase qt6-qtwebsockets qt6-qtwayland"
    echo "On Ubuntu/Debian: sudo apt install qt6-base-dev libqt6websockets6-dev qt6-wayland"
    exit 1
fi

cd "${HERE}"
exec env PYTHONHOME="${MO2_PYTHONHOME}" "${HERE}/ModOrganizer-core" "$@"
LAUNCH
chmod +x "${OUT_DIR}/fluorine-manager"

# ── Summary ──
echo ""
echo "=== Build Summary ==="
du -sh "${OUT_DIR}"/*/ "${OUT_DIR}"/ModOrganizer-core 2>/dev/null | sort -rh
echo ""
echo "Staging complete."

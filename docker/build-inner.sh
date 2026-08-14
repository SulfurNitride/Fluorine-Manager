#!/usr/bin/env bash
set -euo pipefail

BUILD_PY="${BUILD_PYTHON:-$(command -v python3)}"

# ── Build ──
PYBIND11_DIR="$("${BUILD_PY}" -c 'import pybind11; print(pybind11.get_cmake_dir())' 2>/dev/null || true)"

CMAKE_EXTRA_ARGS=()
# FetchContent dependencies are revision-pinned. Reuse an existing populated
# source tree without contacting every remote on each incremental build; an
# absent source is still downloaded during the initial population step.
CMAKE_EXTRA_ARGS+=("-DFETCHCONTENT_UPDATES_DISCONNECTED=ON")
# Always set the runtime directory explicitly. CMake caches this PATH, so
# omitting it after an experimental build would silently repackage that prior
# candidate during a normal reference restore.
CMAKE_EXTRA_ARGS+=(
    "-DFLUORINE_USVFS_RUNTIME_DIR=${FLUORINE_USVFS_RUNTIME_DIR:-/opt/fluorine-usvfs}")
if [ "${BUILD_MODE:-tarball}" = "test" ]; then
    CMAKE_EXTRA_ARGS+=("-DBUILD_TESTING=OFF" "-DBUILD_FLUORINE_TESTING=ON")
fi

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
# Defaults: dev channel, empty build metadata (CMake fills commit from git).
FLUORINE_BUILD_CHANNEL="${FLUORINE_BUILD_CHANNEL:-dev}"
FLUORINE_BUILD_NUMBER="${FLUORINE_BUILD_NUMBER:-}"
FLUORINE_BUILD_TIMESTAMP="${FLUORINE_BUILD_TIMESTAMP:-}"
FLUORINE_BUILD_COMMIT="${FLUORINE_BUILD_COMMIT:-}"

cmake -S . -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DPython_EXECUTABLE="${BUILD_PY}" \
    -DPython_ROOT_DIR="${PYTHON_ROOT}" \
    ${PYBIND11_DIR:+-Dpybind11_DIR="${PYBIND11_DIR}"} \
    -DBUILD_PLUGIN_PYTHON=ON \
    -DFLUORINE_BUILD_CHANNEL="${FLUORINE_BUILD_CHANNEL}" \
    -DFLUORINE_BUILD_NUMBER="${FLUORINE_BUILD_NUMBER}" \
    -DFLUORINE_BUILD_TIMESTAMP="${FLUORINE_BUILD_TIMESTAMP}" \
    -DFLUORINE_BUILD_COMMIT="${FLUORINE_BUILD_COMMIT}" \
    "${CMAKE_EXTRA_ARGS[@]}"

if [ "${BUILD_MODE:-tarball}" = "test" ]; then
    if [ -n "${BUILD_JOBS:-}" ]; then
        cmake --build build --target organizer fluorine-tests --parallel "${BUILD_JOBS}"
    else
        cmake --build build --target organizer fluorine-tests --parallel
    fi
    ctest --test-dir build --output-on-failure
    exit 0
fi

# The build tree is intentionally incremental, but packaging must not discover
# a plugin left behind by a target that no longer exists. Remove only the
# package-shaped outputs; Ninja recreates every output owned by the current
# graph below.
if [ -d build/libs ]; then
    find build/libs -type f \( \
        -name "libgame_*.so" -o \
        -name "libinstaller_*.so" -o \
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
    \) -delete
fi

if [ -n "${BUILD_JOBS:-}" ]; then
    cmake --build build --parallel "${BUILD_JOBS}"
else
    cmake --build build --parallel
fi

MODORG_BIN="build/src/src/ModOrganizer"
if [ ! -f "${MODORG_BIN}" ]; then
    echo "ERROR: ModOrganizer binary not found at ${MODORG_BIN}"
    exit 1
fi
RUNDIR="build/src/src"

# ── Output layout (staging area — installed to ~/.local/share/fluorine by build-native.sh) ──
OUT_DIR="/src/build/staging"
rm -rf "${OUT_DIR}"
mkdir -p "${OUT_DIR}/plugins" "${OUT_DIR}/lib" "${OUT_DIR}/fonts"

mkdir -p "${OUT_DIR}/licenses"

# A distributed GPL binary must carry its license, and users need an offline
# explanation that the extracted archive publishes a managed per-user runtime.
cp -f /src/LICENSE.txt "${OUT_DIR}/LICENSE.txt"
cp -f /src/packaging/README-PORTABLE.txt "${OUT_DIR}/README-PORTABLE.txt"
cp -f /src/docs/installation.md "${OUT_DIR}/INSTALLATION.md"
cp -f /src/packaging/desktop_entry.py "${OUT_DIR}/fluorine-desktop-entry.py"
chmod 755 "${OUT_DIR}/fluorine-desktop-entry.py"
for required_document in LICENSE.txt README-PORTABLE.txt INSTALLATION.md; do
    if [ ! -s "${OUT_DIR}/${required_document}" ]; then
        echo "ERROR: required package document is missing: ${required_document}" >&2
        exit 1
    fi
done

# Preserve the licenses available for source components copied into the
# portable bundle. Names are fixed so later updates retire stale notices.
declare -A SOURCE_LICENSES=(
    [7zip-license]="build/_deps/7zip_src-src/DOC/License.txt"
    [7zip-copying]="build/_deps/7zip_src-src/DOC/copying.txt"
    [archive]="libs/archive/LICENSE"
    [basic-games]="libs/basic_games/LICENSE"
    [blake3-apache]="build/_deps/blake3_upstream-src/LICENSE_A2"
    [blake3-apache-llvm]="build/_deps/blake3_upstream-src/LICENSE_A2LLVM"
    [blake3-cc0]="build/_deps/blake3_upstream-src/LICENSE_CC0"
    [bsa-packer]="libs/bsapacker/LICENSE.md"
    [esp-json]="build/_deps/esp_json-src/LICENSE"
    [esp-json-tes5edit]="build/_deps/esp_json-src/TES5Edit/LICENSE.txt"
    [fnis-tool]="libs/fnistool/LICENSE"
    [form43-checker]="libs/form43_checker/LICENSE"
    [gli]="build/_deps/gli-src/manual.md"
    [installer-bsplugins]="libs/installer_bsplugins/LICENSE"
    [installer-omod]="libs/installer_omod/LICENSE"
    [icu-73.2]="/opt/fluorine-icu-73.2-LICENSE"
    [libbsarch]="libs/libbsarch/LICENSE"
    [libfuse]="/opt/fluorine-libfuse-LICENSE"
    [libfuse-GPL-2]="/opt/fluorine-libfuse-GPL2"
    [libfuse-LGPL-2.1]="/opt/fluorine-libfuse-LGPL2"
    [microsoft-dds]="libs/dds-header/LICENSE"
    [nifly]="build/_deps/nifly-src/LICENSE"
    [preview-dds]="libs/preview_dds/LICENSE"
    [preview-nif]="libs/preview_nif/LICENSE"
    [script-extender-plugin-checker]="libs/script_extender_plugin_checker/LICENSE"
    [steam-vdf-parser-apache]="libs/steam_appinfo_ffi/vendor/steam-vdf-parser/LICENSE-APACHE"
    [steam-vdf-parser-mit]="libs/steam_appinfo_ffi/vendor/steam-vdf-parser/LICENSE-MIT"
    [spdlog]="/usr/share/doc/libspdlog-dev/copyright"
    [utf8proc]="build/_deps/utf8proc_upstream-src/LICENSE.md"
)
for license_name in "${!SOURCE_LICENSES[@]}"; do
    license_source="${SOURCE_LICENSES[${license_name}]}"
    if [ ! -s "${license_source}" ]; then
        echo "ERROR: source license is missing: ${license_source}" >&2
        exit 1
    fi
    cp -f "${license_source}" "${OUT_DIR}/licenses/${license_name}.txt"
done
cp -f /src/packaging/THIRD-PARTY-NOTICES.txt "${OUT_DIR}/licenses/README.txt"

# Rust cdylibs statically contain their target-reachable crates, so retain each
# crate's own copyright/license text rather than only the top-level manifests.
"${BUILD_PY}" /src/docker/collect_cargo_licenses.py \
    --output "${OUT_DIR}/licenses/cargo" \
    /src/libs/bsa_ffi/Cargo.toml \
    /src/libs/steam_appinfo_ffi/Cargo.toml

# The bundled interpreter and selected Python distributions are copied without
# dist-info at runtime. Preserve their notices before pruning that metadata.
PYTHON_LICENSE_ROOT="/opt/python-bundled/lib/python3.12"
PYTHON_SITE="${PYTHON_LICENSE_ROOT}/site-packages"
cp -f "${PYTHON_LICENSE_ROOT}/LICENSE.txt" \
    "${OUT_DIR}/licenses/python-3.12.txt"

copy_single_python_license() {
    local destination_name="$1"
    local source_pattern="$2"
    local matches=()
    mapfile -t matches < <(compgen -G "${source_pattern}" || true)
    if [ "${#matches[@]}" -ne 1 ] || [ ! -s "${matches[0]}" ]; then
        echo "ERROR: expected one Python license matching ${source_pattern}" >&2
        exit 1
    fi
    cp -f "${matches[0]}" "${OUT_DIR}/licenses/${destination_name}.txt"
}

copy_single_python_license psutil "${PYTHON_SITE}/psutil-*.dist-info/LICENSE"
copy_single_python_license vdf "${PYTHON_SITE}/vdf-*.dist-info/LICENSE"
copy_single_python_license pybind11 "${PYTHON_SITE}/pybind11-*.dist-info/LICENSE"
copy_single_python_license pyqt6 \
    "${PYTHON_SITE}/pyqt6-*.dist-info/licenses/LICENSE"
copy_single_python_license pyqt6-qt6 \
    "${PYTHON_SITE}/pyqt6_qt6-*.dist-info/LICENSE"
copy_single_python_license pyqt6-sip \
    "${PYTHON_SITE}/pyqt6_sip-*.dist-info/licenses/LICENSE"

for python_sbom in \
    "${PYTHON_SITE}"/larian_formats-*.dist-info/sboms/*.json \
    "${PYTHON_SITE}"/libloot-*.dist-info/sboms/*.json; do
    if [ ! -s "${python_sbom}" ]; then
        echo "ERROR: required Python SBOM is missing: ${python_sbom}" >&2
        exit 1
    fi
    cp -f "${python_sbom}" \
        "${OUT_DIR}/licenses/$(basename "${python_sbom}")"
done
if [ ! -d /opt/python-bundled/share/licenses/libloot-cargo ]; then
    echo "ERROR: libloot Cargo license payload is missing" >&2
    exit 1
fi
cp -a /opt/python-bundled/share/licenses/libloot-cargo \
    "${OUT_DIR}/licenses/libloot-cargo"
if [ ! -d /opt/python-bundled/share/licenses/larian-formats-cargo ]; then
    echo "ERROR: larian-formats Cargo license payload is missing" >&2
    exit 1
fi
cp -a /opt/python-bundled/share/licenses/larian-formats-cargo \
    "${OUT_DIR}/licenses/larian-formats-cargo"

# Qt's wheel carries the LGPL text above; retain the component-specific SPDX
# SBOMs for every Qt module represented in the pruned runtime.
QT_RUNTIME_VERSION="$(basename "$(dirname "${Qt6_DIR}")")"
mkdir -p "${OUT_DIR}/licenses/qt-sbom"
for qt_component in qtbase qtdeclarative qtimageformats qtnetworkauth qtsvg qtwayland; do
    qt_sbom="${Qt6_DIR}/sbom/${qt_component}-${QT_RUNTIME_VERSION}.spdx.json"
    if [ ! -s "${qt_sbom}" ]; then
        echo "ERROR: Qt SBOM is missing: ${qt_sbom}" >&2
        exit 1
    fi
    cp -f "${qt_sbom}" "${OUT_DIR}/licenses/qt-sbom/"
done

for common_license in Apache-2.0 GPL-3 LGPL-3; do
    if [ ! -s "/usr/share/common-licenses/${common_license}" ]; then
        echo "ERROR: common license text is missing: ${common_license}" >&2
        exit 1
    fi
    cp -f "/usr/share/common-licenses/${common_license}" \
        "${OUT_DIR}/licenses/common-${common_license}.txt"
done

# Fluorine deliberately selects DejaVu Sans through QFontDatabase so its UI
# does not depend on a desktop theme choosing a sensible application font.
# These are application assets, not a private Fontconfig installation: the
# runtime library and configuration remain host-provided below.
DEJAVU_DIR=""
for candidate in \
    /usr/share/fonts/truetype/dejavu \
    /usr/share/fonts/dejavu \
    /usr/share/TTF; do
    if [ -s "${candidate}/DejaVuSans.ttf" ] && \
       [ -s "${candidate}/DejaVuSans-Bold.ttf" ]; then
        DEJAVU_DIR="${candidate}"
        break
    fi
done
if [ -z "${DEJAVU_DIR}" ]; then
    echo "ERROR: required DejaVu Sans application fonts were not found" >&2
    exit 1
fi
for font in DejaVuSans.ttf DejaVuSans-Bold.ttf; do
    cp -f "${DEJAVU_DIR}/${font}" "${OUT_DIR}/fonts/${font}"
    if [ ! -s "${OUT_DIR}/fonts/${font}" ]; then
        echo "ERROR: failed to stage application font ${font}" >&2
        exit 1
    fi
done
DEJAVU_LICENSE=/usr/share/doc/fonts-dejavu-core/copyright
if [ ! -s "${DEJAVU_LICENSE}" ]; then
    echo "ERROR: DejaVu font license was not found" >&2
    exit 1
fi
cp -f "${DEJAVU_LICENSE}" "${OUT_DIR}/licenses/fonts-dejavu-core.txt"
if [ ! -s "${OUT_DIR}/licenses/fonts-dejavu-core.txt" ]; then
    echo "ERROR: failed to stage the DejaVu font license" >&2
    exit 1
fi

# ── Main binary + helpers ──
cp -f "${RUNDIR}/ModOrganizer" "${OUT_DIR}/ModOrganizer-core"
[ -f "/src/src/fluorine-manager" ] && cp -f "/src/src/fluorine-manager" "${OUT_DIR}/"

# Wine-side USVFS controller and pinned x64/x86 injection runtime. Stage only
# the five owned runtime leaves so incremental build-tree residue cannot enter
# the typed package manifest.
mkdir -p "${OUT_DIR}/usvfs"
for usvfs_leaf in \
    fluorine-usvfs-launcher.exe \
    usvfs_x64.dll usvfs_x86.dll \
    usvfs_proxy_x64.exe usvfs_proxy_x86.exe; do
    if [ ! -f "${RUNDIR}/usvfs/${usvfs_leaf}" ]; then
        echo "ERROR: Wine-side USVFS runtime is missing ${usvfs_leaf}"
        exit 1
    fi
    cp -f "${RUNDIR}/usvfs/${usvfs_leaf}" "${OUT_DIR}/usvfs/${usvfs_leaf}"
done
if [ -n "${FLUORINE_USVFS_PROVENANCE:-}" ]; then
    if [ ! -f "${FLUORINE_USVFS_PROVENANCE}" ]; then
        echo "ERROR: USVFS provenance file is missing: ${FLUORINE_USVFS_PROVENANCE}"
        exit 1
    fi
    cp -f "${FLUORINE_USVFS_PROVENANCE}" \
        "${OUT_DIR}/usvfs/fluorine-candidate-build.txt"
fi
cp -f /opt/fluorine-usvfs/LICENSE "${OUT_DIR}/licenses/usvfs-GPL-3.0.txt"

# wrestool/icotool no longer needed — icon extraction is built into the C++ PE parser

# ── MO2 plugins (.so) ──
find build/libs -type f \( \
    -name "libgame_*.so" -o \
    -name "libinstaller_*.so" -o \
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

# basic_games Python module. Copy only runtime Python source so ignored local
# files, caches and repository metadata cannot enter a release manifest.
if [ -d "libs/basic_games" ]; then
    while IFS= read -r -d '' source_file; do
        relative_path="${source_file#libs/basic_games/}"
        destination="${OUT_DIR}/plugins/basic_games/${relative_path}"
        mkdir -p "$(dirname "${destination}")"
        cp -f -- "${source_file}" "${destination}"
    done < <(find "libs/basic_games" -type f -name '*.py' -print0)
fi
# data/ dir (DDS headers etc., used by DDSPreview.py via plugins/data/ in sys.path).
# Stage its Python source explicitly (cmake staging is OFF by default).
if [ -d "libs/preview_dds/src/DDS" ]; then
    while IFS= read -r -d '' source_file; do
        relative_path="${source_file#libs/preview_dds/src/DDS/}"
        destination="${OUT_DIR}/plugins/data/DDS/${relative_path}"
        mkdir -p "$(dirname "${destination}")"
        cp -f -- "${source_file}" "${destination}"
    done < <(find "libs/preview_dds/src/DDS" -type f -name '*.py' -print0)
fi

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
if [ -d "src/src/stylesheets" ]; then
    while IFS= read -r -d '' source_file; do
        relative_path="${source_file#src/src/stylesheets/}"
        destination="${OUT_DIR}/stylesheets/${relative_path}"
        mkdir -p "$(dirname "${destination}")"
        cp -f -- "${source_file}" "${destination}"
    done < <(find "src/src/stylesheets" -type f \
        \( -name '*.qss' -o -name '*.png' \) -print0)
    echo "Bundled stylesheets"
fi

# ── Interactive tutorials ──
# TutorialManager resolves these at runtime relative to the application binary.
# CMake's development run tree has them, but the portable staging step must copy
# them explicitly alongside ModOrganizer-core.
if [ -d "src/src/tutorials" ]; then
    while IFS= read -r -d '' source_file; do
        relative_path="${source_file#src/src/tutorials/}"
        destination="${OUT_DIR}/tutorials/${relative_path}"
        mkdir -p "$(dirname "${destination}")"
        cp -f -- "${source_file}" "${destination}"
    done < <(find "src/src/tutorials" -type f \
        \( -name '*.js' -o -name '*.qml' \) -print0)
    echo "Bundled tutorials"
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
SKIP_PATTERN="linux-vdso|ld-linux|libc\.so|libm\.so|libdl\.so|librt\.so|libpthread|libresolv|libutil\.so|libnss|libgcc_s|libstdc\+\+"
# GPU/graphics drivers must be host-provided
SKIP_PATTERN="${SKIP_PATTERN}|libGL\.so|libEGL|libGLX|libGLdispatch|libdrm|libgbm|libvulkan|libX11"
SKIP_PATTERN="${SKIP_PATTERN}|libxcb\.so(\.|$)|libxcb-(glx|randr|render|shape|shm|sync|xfixes|xkb)\.so(\.|$)"
SKIP_PATTERN="${SKIP_PATTERN}|libwayland-client|libwayland-server|libwayland-cursor|libwayland-egl|libxkbcommon"
# libpython — user provides via system Python; do not bundle.
SKIP_PATTERN="${SKIP_PATTERN}|libpython"
# OpenSSL should come from the host so we don't pin users to a stale TLS stack.
SKIP_PATTERN="${SKIP_PATTERN}|libssl\.so|libcrypto\.so"
# Fontconfig's runtime and its distribution-owned configuration files are one
# compatibility unit. Bundling the library while reading a newer host's
# /usr/share/fontconfig/conf.avail produces parser warnings (and can reject
# newer rules). Use the host library with the matching host configuration.
SKIP_PATTERN="${SKIP_PATTERN}|libfontconfig\.so"

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
    # These optional plugins are not part of Fluorine's supported runtime
    # boundary. qgtk3 recursively binds the bundle to the build host's GTK
    # stack, while aqt's TIFF plugin requires obsolete libtiff.so.5. Qt falls
    # back to its own platform theme and the remaining image formats.
    rm -f "${OUT_DIR}/qt6plugins/platformthemes/libqgtk3.so"
    rm -f "${OUT_DIR}/qt6plugins/imageformats/libqtiff.so"
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
# Bundle the matching interpreter for isolated helpers plus lib/python3.12/.
# Headers, static libraries, and source-only development files remain excluded.
PBS_SRC="/opt/python-bundled"
PYTHON_OUT="${OUT_DIR}/python"
mkdir -p "${PYTHON_OUT}/bin" "${PYTHON_OUT}/lib"
cp -Lf "${PBS_SRC}/bin/python3.12" "${PYTHON_OUT}/bin/python3.12"
cat > "${PYTHON_OUT}/bin/python3" <<'PYTHON_LAUNCHER'
#!/usr/bin/env sh
exec "$(dirname "$0")/python3.12" "$@"
PYTHON_LAUNCHER
chmod 755 "${PYTHON_OUT}/bin/python3"

# Copy only the stdlib directory
cp -a "${PBS_SRC}/lib/python3.12" "${PYTHON_OUT}/lib/"

# Remove only what is safe — test suites, GUI toolkits, dev tools.
# Do NOT strip network/stdlib modules; basic_games uses email, http, xml, urllib, etc.
find "${PYTHON_OUT}" -type d \( -name "test" -o -name "tests" \) \
    -exec rm -rf {} + 2>/dev/null || true
rm -rf "${PYTHON_OUT}/lib/python3.12/tkinter"
rm -f "${PYTHON_OUT}/lib/python3.12/lib-dynload/"_tkinter*.so
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
# Copy runtime-required packages back in.
# `loot` is libloot's Python binding (a single loot*.so extension), used by the
# OpenMW "Sort with LOOT" tool. It is optional: if the build image didn't
# produce it, the find_spec lookup returns nothing and we simply skip it — the
# tool then degrades gracefully at runtime.
for pkg in psutil vdf larian_formats loot; do
    pkg_dir="$("${PBS_SRC}/bin/python3" -c "import importlib.util; s=importlib.util.find_spec('${pkg}'); print(s.submodule_search_locations[0] if s and s.submodule_search_locations else (s.origin if s else ''))" 2>/dev/null || true)"
    if [ -d "${pkg_dir}" ]; then
        cp -a "${pkg_dir}" "${PYTHON_OUT}/lib/python3.12/site-packages/"
    elif [ -f "${pkg_dir}" ]; then
        cp -f "${pkg_dir}" "${PYTHON_OUT}/lib/python3.12/site-packages/"
    fi
done

# Bundle non-system dependencies of every Python extension. These objects are
# loaded dynamically and therefore were not covered by the earlier executable
# and plugin scan (for example, CPython's _crypt module needs libcrypt).
find "${PYTHON_OUT}/lib/python3.12" -type f -name "*.so" 2>/dev/null | \
while read -r python_extension; do
    ldd "${python_extension}" 2>/dev/null | grep "=>" | awk '{print $3}' | grep "^/" | while read -r dep; do
        dep_name="$(basename "${dep}")"
        echo "${dep_name}" | grep -qE "${SKIP_PATTERN}" && continue
        [ -f "${OUT_DIR}/lib/${dep_name}" ] && continue
        cp -Lf "${dep}" "${OUT_DIR}/lib/" 2>/dev/null && \
            echo "  + ${dep_name} (Python extension dependency)" || true
    done
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
cp -f /src/packaging/fluorine_publisher.py "${OUT_DIR}/fluorine-publisher.py"
chmod 755 "${OUT_DIR}/fluorine-publisher.py"
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
# Python plugins only import the modules below. Pruning happens BEFORE
# the deps-scan loop further down so libQt6Designer/Help/Sql/Test/SvgWidgets/
# Bluetooth/... stop getting copied into lib/ as a side effect.
PYQT6_KEEP_MODULES="QtCore QtGui QtWidgets QtNetwork QtOpenGL QtOpenGLWidgets"
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
find "${OUT_DIR}/plugins/libs" -maxdepth 1 -name "*.so" -exec patchelf --force-rpath --set-rpath '$ORIGIN/../../lib' {} \; 2>/dev/null || true
find "${OUT_DIR}/plugins/libs/PyQt6" -name "*.so" -exec patchelf --force-rpath --set-rpath '$ORIGIN/../../../lib' {} \; 2>/dev/null || true
find "${OUT_DIR}/lib" \( -name "*.so" -o -name "*.so.*" \) -exec patchelf --force-rpath --set-rpath '$ORIGIN' {} \; 2>/dev/null || true
# Qt platform plugins keep aqtinstall's hardcoded RPATH (/opt/qt6/.../lib) which
# doesn't exist on user systems — the linker falls through to system Qt, loading
# the wrong version and poisoning the link map for all subsequent Qt library
# lookups (including PyQt6 bindings).  All Qt plugins sit one subdir deep under
# qt6plugins/, so $ORIGIN/../../lib resolves correctly to our lib/ for all of them.
find "${OUT_DIR}/qt6plugins" -name "*.so" -exec patchelf --force-rpath --set-rpath '$ORIGIN/../../lib' {} \; 2>/dev/null || true

# Most non-Qt libraries copied from the builder are distribution packages.
# Preserve the exact package copyright file for every staged basename that can
# be resolved to a builder-owned library. Qt, Python, Rust, libfuse and the
# source-built components have their upstream notices staged above.
declare -A SYSTEM_LICENSE_PACKAGES=()
for bundled_library in "${OUT_DIR}"/lib/*.so*; do
    [ -e "${bundled_library}" ] || continue
    library_name="$(basename "${bundled_library}")"
    case "${library_name}" in
        libQt6*|libicu*|libpython*|libfuse3*) continue ;;
    esac
    for system_library in \
        "/lib/x86_64-linux-gnu/${library_name}" \
        "/usr/lib/x86_64-linux-gnu/${library_name}"; do
        [ -e "${system_library}" ] || continue
        resolved_library="$(readlink -f "${system_library}")"
        ownership="$(
            dpkg-query -S "${system_library}" 2>/dev/null | head -n 1 || true
        )"
        if [ -z "${ownership}" ]; then
            ownership="$(
                dpkg-query -S "${resolved_library}" 2>/dev/null | head -n 1 || true
            )"
        fi
        if [ -n "${ownership}" ]; then
            package_with_arch="$(printf '%s\n' "${ownership}" | sed 's|: /.*$||')"
            package_name="${package_with_arch%%:*}"
            SYSTEM_LICENSE_PACKAGES["${package_name}"]=1
            break
        fi
    done
done

mkdir -p "${OUT_DIR}/licenses/system"
system_license_count=0
while IFS= read -r package_name; do
    [ -n "${package_name}" ] || continue
    copyright_file="/usr/share/doc/${package_name}/copyright"
    if [ ! -s "${copyright_file}" ]; then
        echo "ERROR: package copyright file is missing: ${copyright_file}" >&2
        exit 1
    fi
    cp -Lf "${copyright_file}" \
        "${OUT_DIR}/licenses/system/${package_name}.txt"
    system_license_count=$((system_license_count + 1))
done < <(printf '%s\n' "${!SYSTEM_LICENSE_PACKAGES[@]}" | sort)
if [ "${system_license_count}" -eq 0 ]; then
    echo "ERROR: no distribution library copyright files were staged" >&2
    exit 1
fi

# Every non-host dependency must now resolve through the completed bundle.
# This is intentionally after all pruning, dependency collection and RPATH
# edits, and before the typed manifest authenticates the result.
"${BUILD_PY}" /src/packaging/elf_dependency_policy.py "${OUT_DIR}"

# ── Launcher script ──
cat > "${OUT_DIR}/fluorine-manager" <<'LAUNCH'
#!/usr/bin/env bash
set -euo pipefail
SELF="$(readlink -f "$0")"
HERE="$(cd "$(dirname "$SELF")" && pwd)"

# Save the original environment so launched games and setup tools can restore
# it. Without this, Fluorine's bundled-runtime policy leaks into child
# processes and can cause library conflicts.
[[ -v FLUORINE_ORIG_LD_LIBRARY_PATH ]] || \
    FLUORINE_ORIG_LD_LIBRARY_PATH="${LD_LIBRARY_PATH:-}"
[[ -v FLUORINE_ORIG_LD_PRELOAD ]] || \
    FLUORINE_ORIG_LD_PRELOAD="${LD_PRELOAD:-}"
[[ -v FLUORINE_ORIG_PATH ]] || FLUORINE_ORIG_PATH="${PATH}"
[[ -v FLUORINE_ORIG_XDG_DATA_DIRS ]] || \
    FLUORINE_ORIG_XDG_DATA_DIRS="${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"
[[ -v FLUORINE_ORIG_QT_PLUGIN_PATH ]] || \
    FLUORINE_ORIG_QT_PLUGIN_PATH="${QT_PLUGIN_PATH:-}"
export FLUORINE_ORIG_LD_LIBRARY_PATH FLUORINE_ORIG_LD_PRELOAD
export FLUORINE_ORIG_PATH FLUORINE_ORIG_XDG_DATA_DIRS
export FLUORINE_ORIG_QT_PLUGIN_PATH

# Clear any injected preload for the bundled Qt6 process. Game launches restore
# the original value via FLUORINE_ORIG_LD_PRELOAD.
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

PUBLISH_ONLY=0
CLEAN_UPDATE=""
PUBLISH_WAIT=0
while true; do
    case "${1:-}" in
        --fluorine-publish-only)
            PUBLISH_ONLY=1
            shift
            ;;
        --fluorine-clean-update=*)
            CLEAN_UPDATE="${1#--fluorine-clean-update=}"
            shift
            ;;
        --fluorine-wait-publish=*)
            PUBLISH_WAIT="${1#--fluorine-wait-publish=}"
            shift
            ;;
        *) break ;;
    esac
done

run_publisher() {
    local ROOT="$1"
    shift
    local PYTHON="${ROOT}/python/bin/python3"
    local PUBLISHER="${ROOT}/fluorine-publisher.py"
    if [ ! -x "${PYTHON}" ] || [ ! -f "${PUBLISHER}" ]; then
        echo "ERROR: Fluorine publication tools are missing from ${ROOT}." >&2
        return 1
    fi
    env \
        LD_LIBRARY_PATH="${ROOT}/lib" \
        PYTHONHOME="${ROOT}/python" \
        PYTHONPATH= PYTHONNOUSERSITE=1 \
        "${PYTHON}" "${PUBLISHER}" "$@"
}

# Recover through the immutable staged runtime, never through Python or
# libraries in the installation being replaced.
RECOVERY_ROOT=""
RECOVERY_POINTER="${FLUORINE_DATA}/publish-recovery"
if [ -f "${RECOVERY_POINTER}" ] && [ ! -L "${RECOVERY_POINTER}" ]; then
    IFS= read -r RECOVERY_ID < "${RECOVERY_POINTER}" || true
    if [[ "${RECOVERY_ID:-}" =~ ^[0-9a-f]{64}$ ]]; then
        CANDIDATE="${FLUORINE_DATA}/publish-stage/${RECOVERY_ID}"
        if [ -d "${CANDIDATE}" ] && [ ! -L "${CANDIDATE}" ]; then
            RECOVERY_ROOT="${CANDIDATE}"
        fi
    fi
    if [ -z "${RECOVERY_ROOT}" ]; then
        echo "ERROR: Fluorine's publication recovery pointer is invalid." >&2
        echo "Keep the update files and repair ${FLUORINE_DATA} before launching." >&2
        exit 1
    fi
fi

if [ -n "${RECOVERY_ROOT}" ]; then
    run_publisher "${RECOVERY_ROOT}" publish \
        "${RECOVERY_ROOT}" "${BIN_DST}" "${FLUORINE_DATA}" \
        --wait-runtime "${PUBLISH_WAIT}"
fi

# A source bundle publishes exact, verified leaves through one serialized,
# restartable transaction. A launcher already in BIN_DST must prove that the
# prior transaction committed before starting the core.
HERE_REAL="$(readlink -f "${HERE}")"
DST_REAL="$(readlink -f "${BIN_DST}" 2>/dev/null || echo "")"
if [ "${HERE_REAL}" != "${DST_REAL}" ]; then
    if [ ! -f "${HERE}/fluorine-manifest-v2.json" ] || \
       [ ! -f "${HERE}/ModOrganizer-core" ]; then
        echo "ERROR: Fluorine launcher can't find its bundle files in ${HERE}." >&2
        echo "Extract the release archive into its own directory and run the" >&2
        echo "launcher from there — not from a folder containing other files." >&2
        exit 1
    fi
    echo "Publishing Fluorine to ${BIN_DST}..." >&2
    run_publisher "${HERE}" publish \
        "${HERE}" "${BIN_DST}" "${FLUORINE_DATA}" \
        --wait-runtime "${PUBLISH_WAIT}"
    echo "Publication complete." >&2
else
    run_publisher "${HERE}" check-installed "${BIN_DST}" "${FLUORINE_DATA}"
fi

if [ -n "${CLEAN_UPDATE}" ]; then
    STAGING_ROOT="$(readlink -m "${FLUORINE_DATA}/update-staging")"
    CLEAN_REAL="$(readlink -m "${CLEAN_UPDATE}")"
    case "${CLEAN_REAL}" in
        "${STAGING_ROOT}"/attempt-*) rm -rf -- "${CLEAN_REAL}" ;;
        *) echo "WARNING: refusing unsafe update cleanup path ${CLEAN_UPDATE}" >&2 ;;
    esac
fi

# ── Install icon + desktop file for Wayland taskbar/decoration ──
ICON_SRC="${BIN_DST}/icons/com.fluorine.manager.png"
ICON_DST="${HOME}/.local/share/icons/hicolor/256x256/apps/com.fluorine.manager.png"
DESKTOP_SRC="${BIN_DST}/icons/com.fluorine.manager.desktop"
DESKTOP_DST="${HOME}/.local/share/applications/com.fluorine.manager.desktop"
DESKTOP_RENDERER="${BIN_DST}/fluorine-desktop-entry.py"
INTEGRATION_FAILED=0
if [ -f "${ICON_SRC}" ]; then
    if mkdir -p "$(dirname "${ICON_DST}")"; then
        if ICON_TMP="$(mktemp "${ICON_DST}.tmp.XXXXXX")"; then
            if cp -f -- "${ICON_SRC}" "${ICON_TMP}" && \
               chmod 644 "${ICON_TMP}" && \
               mv -fT "${ICON_TMP}" "${ICON_DST}"; then
                :
            else
                echo "WARNING: could not install Fluorine desktop icon." >&2
                rm -f -- "${ICON_TMP}"
                INTEGRATION_FAILED=1
            fi
        else
            echo "WARNING: could not create a temporary desktop icon." >&2
            INTEGRATION_FAILED=1
        fi
    else
        echo "WARNING: could not create the desktop icon directory." >&2
        INTEGRATION_FAILED=1
    fi
else
    echo "WARNING: Fluorine desktop icon is missing." >&2
    INTEGRATION_FAILED=1
fi
if [ -f "${DESKTOP_SRC}" ] && [ -f "${DESKTOP_RENDERER}" ]; then
    if mkdir -p "$(dirname "${DESKTOP_DST}")"; then
        # This file belongs to Fluorine, so refresh it on every launch. Older
        # releases advertised the main desktop entry as an NXM handler but did
        # not include %u; leaving that file in place causes portals to drop the
        # URL and launch a second argument-less process.
        if env \
            LD_LIBRARY_PATH="${BIN_DST}/lib" \
            PYTHONHOME="${BIN_DST}/python" \
            PYTHONPATH= PYTHONNOUSERSITE=1 \
            "${BIN_DST}/python/bin/python3" "${DESKTOP_RENDERER}" \
            "${DESKTOP_SRC}" "${DESKTOP_DST}" \
            "${BIN_DST}/fluorine-manager"; then
            command -v update-desktop-database >/dev/null 2>&1 && \
                update-desktop-database "$(dirname "${DESKTOP_DST}")" \
                    >/dev/null 2>&1 || true
        else
            echo "WARNING: could not install Fluorine desktop entry." >&2
            INTEGRATION_FAILED=1
        fi
    else
        echo "WARNING: could not create the desktop entry directory." >&2
        INTEGRATION_FAILED=1
    fi
else
    echo "WARNING: Fluorine desktop integration files are missing." >&2
    INTEGRATION_FAILED=1
fi

if [ "${PUBLISH_ONLY}" -eq 1 ]; then
    exit "${INTEGRATION_FAILED}"
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
if ! command -v flock >/dev/null 2>&1; then
    echo "ERROR: Fluorine requires the util-linux flock command." >&2
    exit 1
fi
# The shell opens and locks the descriptor itself, then execs the core with the
# same PID. The core validates/owns this exact descriptor for its full lifetime
# and marks it close-on-exec before launching games or helpers. This avoids a
# flock supervisor that would swallow SIGTERM and release the lease early.
exec {FLUORINE_RUNTIME_LOCK_FD}<>"${FLUORINE_DATA}/runtime.lock"
flock --shared "${FLUORINE_RUNTIME_LOCK_FD}"
export FLUORINE_RUNTIME_LOCK_FD

# Recheck the committed generation while the shared lease is already held.
# Publication cannot begin between this proof and the core's adoption of the
# inherited open-file description.
run_publisher "${RUN}" verify-committed "${BIN_DST}" "${FLUORINE_DATA}"
exec "${RUN}/ModOrganizer-core" "$@"
LAUNCH
chmod +x "${OUT_DIR}/fluorine-manager"

# Fontconfig must remain host-provided. It always parses the compile-time
# template directory even when FONTCONFIG_FILE is overridden, so a staged copy
# would recreate the cross-version configuration mismatch this policy avoids.
FONTCONFIG_RUNTIME="$({
    LD_LIBRARY_PATH="${OUT_DIR}/lib" ldd "${OUT_DIR}/ModOrganizer-core" 2>/dev/null \
        | awk '/libfontconfig\.so\.1/ { print $3; exit }'
} || true)"
if [ -z "${FONTCONFIG_RUNTIME}" ]; then
    echo "ERROR: ModOrganizer-core could not resolve host libfontconfig.so.1" >&2
    exit 1
fi
case "${FONTCONFIG_RUNTIME}" in
    "${OUT_DIR}/lib/"*)
        echo "ERROR: portable bundle unexpectedly contains libfontconfig" >&2
        exit 1
        ;;
esac
if find "${OUT_DIR}/lib" -maxdepth 1 \
        \( -name 'libfontconfig.so' -o -name 'libfontconfig.so.*' \) \
        | grep -q .; then
    echo "ERROR: portable bundle unexpectedly staged libfontconfig" >&2
    exit 1
fi

# Validate the exact application-font assets independently of Fontconfig's
# matching policy. QFontDatabase loads these files directly at startup; fc-scan
# is used here only to catch an absent, swapped, or corrupt packaged face.
REGULAR_FONT_METADATA="$(
    fc-scan --format='%{family[0]}|%{style[0]}' \
        "${OUT_DIR}/fonts/DejaVuSans.ttf"
)"
BOLD_FONT_METADATA="$(
    fc-scan --format='%{family[0]}|%{style[0]}' \
        "${OUT_DIR}/fonts/DejaVuSans-Bold.ttf"
)"
if [ "${REGULAR_FONT_METADATA}" != 'DejaVu Sans|Book' ]; then
    echo "ERROR: unexpected regular application font: ${REGULAR_FONT_METADATA}" >&2
    exit 1
fi
if [ "${BOLD_FONT_METADATA}" != 'DejaVu Sans|Bold' ]; then
    echo "ERROR: unexpected bold application font: ${BOLD_FONT_METADATA}" >&2
    exit 1
fi
if find "${OUT_DIR}/fonts" -maxdepth 1 -name 'DejaVuSansMono*.ttf' | grep -q .; then
    echo "ERROR: unused DejaVu Sans Mono assets were staged" >&2
    exit 1
fi
if [ -e "${OUT_DIR}/etc/fonts/fonts.conf" ]; then
    echo "ERROR: private Fontconfig configuration was unexpectedly staged" >&2
    exit 1
fi
bash -n "${OUT_DIR}/fluorine-manager"
echo "Host fontconfig policy verified: ${FONTCONFIG_RUNTIME}; application fonts: ${REGULAR_FONT_METADATA}, ${BOLD_FONT_METADATA}"

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

# ── Typed leaf manifest and content-derived identity ──
# The publisher owns only these exact leaves. Removed nested libraries/plugins
# can therefore be retired without deleting user additions in the same trees;
# modes and symlink targets participate in the identity as well as file bytes.
"${BUILD_PY}" /src/packaging/fluorine_publisher.py \
    build-manifest "${OUT_DIR}"
echo "Wrote typed manifest: $(wc -c < "${OUT_DIR}/fluorine-manifest-v2.json") bytes"

# ── Determine build mode ──
# BUILD_MODE is passed from build.sh: tarball (default), installer, all
BUILD_MODE="${BUILD_MODE:-tarball}"

# ── Build relocatable release distribution (directory) ──
# No archive is created locally. CI wraps this exact directory in .tar.gz.
build_tarball() {
    echo ""
    echo "=== Building relocatable release distribution ==="
    cd /src/build
    TARBALL_NAME="fluorine-manager"
    rm -rf "${TARBALL_NAME}"
    cp -a staging "${TARBALL_NAME}"
    echo "Output: /src/build/${TARBALL_NAME}/"
    du -sh "/src/build/${TARBALL_NAME}"
}

# ── Build self-extracting installer (.bin frontloader) ──
build_installer() (
    echo ""
    echo "=== Building installer ==="

    cd /src/build
    TARBALL_NAME="fluorine-manager"
    INSTALLER_WORK_ROOT="$(mktemp -d /src/build/.fluorine-installer-payload.XXXXXX)"
    trap 'rm -rf -- "${INSTALLER_WORK_ROOT}"' EXIT

    # Create the installer header script
    INSTALLER_SCRIPT="${INSTALLER_WORK_ROOT}/installer-header.sh"
    cat > "${INSTALLER_SCRIPT}" <<'INSTALLER_HEADER'
#!/usr/bin/env bash
set -euo pipefail

APP_NAME="Fluorine Manager"
FLUORINE_DATA="${HOME}/.local/share/fluorine"
INSTALL_DIR="${FLUORINE_DATA}/bin"
DESKTOP_DIR="${HOME}/.local/share/applications"
ICON_DIR="${HOME}/.local/share/icons/hicolor/256x256/apps"

extract_payload() {
    local TARGET="$1"
    local ARCHIVE_START
    ARCHIVE_START=$(awk '/^__PAYLOAD__$/{print NR + 1; exit 0;}' "$0")
    tail -n +"${ARCHIVE_START}" "$0" | tar xzf - -C "${TARGET}"
}

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
    echo "  2) Extract release bundle here (./fluorine-manager/)"
    echo "  3) Cancel"
    echo ""
    read -rp "Choose [1/2/3]: " CHOICE
else
    echo "  1) Install to ${INSTALL_DIR} (per-user)"
    echo "  2) Extract release bundle here (./fluorine-manager/)"
    echo "  3) Cancel"
    echo ""
    read -rp "Choose [1/2/3]: " CHOICE
fi

case "${CHOICE}" in
    1)
        echo ""
        echo "Installing to ${INSTALL_DIR}..."
        mkdir -p "${FLUORINE_DATA}/installer-stage"
        chmod 700 "${FLUORINE_DATA}/installer-stage"
        INSTALL_STAGE=$(mktemp -d "${FLUORINE_DATA}/installer-stage/attempt-XXXXXX")
        trap 'rm -rf -- "${INSTALL_STAGE}"' EXIT
        extract_payload "${INSTALL_STAGE}"
        BUNDLE_ROOT="${INSTALL_STAGE}/fluorine-manager"
        if [ ! -x "${BUNDLE_ROOT}/fluorine-manager" ] || \
           [ ! -f "${BUNDLE_ROOT}/fluorine-manifest-v2.json" ]; then
            echo "ERROR: installer payload is not a valid Fluorine bundle" >&2
            exit 1
        fi
        "${BUNDLE_ROOT}/fluorine-manager" --fluorine-publish-only

        DESKTOP_TARGET="${DESKTOP_DIR}/com.fluorine.manager.desktop"
        ICON_TARGET="${ICON_DIR}/com.fluorine.manager.png"
        if [ ! -f "${DESKTOP_TARGET}" ] || [ -L "${DESKTOP_TARGET}" ] || \
           [ ! -f "${ICON_TARGET}" ] || [ -L "${ICON_TARGET}" ]; then
            echo "ERROR: launcher did not install desktop integration" >&2
            exit 1
        fi
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
        echo "Extracting release bundle to ${PORTABLE_DIR}..."
        if [ -L "${PORTABLE_DIR}" ]; then
            echo "ERROR: ${PORTABLE_DIR} is a symlink; refusing extraction." >&2
            exit 1
        fi
        if [ -e "${PORTABLE_DIR}" ] && [ ! -d "${PORTABLE_DIR}" ]; then
            echo "ERROR: ${PORTABLE_DIR} is not a directory." >&2
            exit 1
        fi
        if [ -d "${PORTABLE_DIR}" ] && \
           [ -n "$(find "${PORTABLE_DIR}" -mindepth 1 -maxdepth 1 -print -quit 2>/dev/null)" ]; then
            echo "ERROR: ${PORTABLE_DIR} is not empty; refusing an in-place overlay." >&2
            exit 1
        fi
        PORTABLE_PARENT="$(dirname "${PORTABLE_DIR}")"
        PORTABLE_STAGE=$(mktemp -d "${PORTABLE_PARENT}/.fluorine-portable-XXXXXX")
        trap 'rm -rf -- "${PORTABLE_STAGE}"' EXIT
        extract_payload "${PORTABLE_STAGE}"
        if [ ! -x "${PORTABLE_STAGE}/fluorine-manager/fluorine-manager" ]; then
            echo "ERROR: installer payload is missing its launcher" >&2
            exit 1
        fi
        rmdir "${PORTABLE_DIR}" 2>/dev/null || true
        mv -T "${PORTABLE_STAGE}/fluorine-manager" "${PORTABLE_DIR}"

        echo ""
        echo "Release bundle extraction complete!"
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

    bash -n "${INSTALLER_SCRIPT}"

    # Build the installer payload in a private work directory. In particular,
    # do not reuse the public release directory produced by build_tarball().
    cp -a staging "${INSTALLER_WORK_ROOT}/${TARBALL_NAME}"
    tar czf "${INSTALLER_WORK_ROOT}/${TARBALL_NAME}-payload.tar.gz" \
        -C "${INSTALLER_WORK_ROOT}" "${TARBALL_NAME}"/

    # Combine header + payload into self-extracting .bin
    cat "${INSTALLER_SCRIPT}" \
        "${INSTALLER_WORK_ROOT}/${TARBALL_NAME}-payload.tar.gz" \
        > "${TARBALL_NAME}.bin"
    chmod +x "${TARBALL_NAME}.bin"

    echo "Installer: /src/build/${TARBALL_NAME}.bin"
    ls -lh "/src/build/${TARBALL_NAME}.bin"
)

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

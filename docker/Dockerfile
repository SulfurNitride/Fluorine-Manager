FROM ubuntu:25.10

ENV DEBIAN_FRONTEND=noninteractive
ENV LANG=C.UTF-8
ENV RUSTUP_HOME=/opt/rust/rustup
ENV CARGO_HOME=/opt/rust/cargo
ENV PATH="/opt/rust/cargo/bin:${PATH}"

# ── System build tools + all library dependencies ──
# Python comes from uv (python-build-standalone), not apt.
RUN apt-get update && apt-get install -y --no-install-recommends \
    # Build essentials
    build-essential cmake meson ninja-build ccache mingw-w64 \
    git curl wget file binutils patchelf pkg-config ca-certificates \
    fonts-dejavu-core \
    # Library dependencies
    libboost-all-dev \
    libgtest-dev \
    libsqlite3-dev \
    libtinyxml2-dev \
    libfontconfig1-dev \
    libspdlog-dev \
    libfuse3-dev fuse3 libcap-dev libnuma-dev \
    liblz4-dev zlib1g-dev libzstd-dev libbz2-dev liblzma-dev \
    libssl-dev libcurl4-openssl-dev \
    libtomlplusplus-dev \
    # Qt 6 runtime deps (aqtinstall provides Qt 6.11 headers/tools/libs for build)
    kde-style-breeze \
    libgl-dev libopengl-dev libvulkan-dev libxkbcommon-dev libfontconfig1-dev \
    libfreetype-dev libx11-dev libxext-dev libxfixes-dev \
    libxi-dev libxrender-dev libxcb1-dev libxcb-cursor-dev \
    libxcb-glx0-dev libxcb-keysyms1-dev libxcb-image0-dev \
    libxcb-shm0-dev libxcb-icccm4-dev libxcb-sync-dev \
    libxcb-xfixes0-dev libxcb-shape0-dev libxcb-randr0-dev \
    libxcb-render-util0-dev libxcb-xinerama0-dev \
    libxcb-xkb-dev libxkbcommon-x11-dev libatspi2.0-dev \
    libwayland-dev \
    # Misc
    zip unzip p7zip-full \
    # Static analysis
    clang-tidy \
    # WebP support for Qt image format plugin
    libwebp-dev \
    && rm -rf /var/lib/apt/lists/*

# ── Wine-side USVFS runtime ──
# Pin the fork's full-matrix release containing the Wine short-name change and
# resolved-snapshot bulk import over upstream 0.5.7.2. The helper compiled by
# Fluorine remains separate so updates stay auditable.
ARG USVFS_RELEASE_TAG=v0.5.7.2-wine-snapshot.2
ARG USVFS_ARCHIVE_SHA256=e6c38a64a2c6b23cc07411180a8958e026c362e3662f1df6542a72f4adcb6ecf
ARG USVFS_LICENSE_SHA256=6ade493f62fb87b3a2fc83d681eb5cb84e1a7a17d426b0277490c0d618017935
RUN mkdir -p /opt/fluorine-usvfs /tmp/usvfs-extract && \
    curl -L --fail --retry 3 \
      "https://github.com/SulfurNitride/usvfs/releases/download/${USVFS_RELEASE_TAG}/usvfs_${USVFS_RELEASE_TAG}.7z" \
      -o /tmp/usvfs.7z && \
    echo "${USVFS_ARCHIVE_SHA256}  /tmp/usvfs.7z" | sha256sum -c - && \
    7z x -y /tmp/usvfs.7z -o/tmp/usvfs-extract >/dev/null && \
    cp /tmp/usvfs-extract/bin/usvfs_x64.dll \
       /tmp/usvfs-extract/bin/usvfs_x86.dll \
       /tmp/usvfs-extract/bin/usvfs_proxy_x64.exe \
       /tmp/usvfs-extract/bin/usvfs_proxy_x86.exe \
       /opt/fluorine-usvfs/ && \
    curl -L --fail --retry 3 \
      "https://raw.githubusercontent.com/SulfurNitride/usvfs/${USVFS_RELEASE_TAG}/LICENSE" \
      -o /opt/fluorine-usvfs/LICENSE && \
    echo "${USVFS_LICENSE_SHA256}  /opt/fluorine-usvfs/LICENSE" | sha256sum -c - && \
    rm -rf /tmp/usvfs.7z /tmp/usvfs-extract

# Build a pinned upstream libfuse so Fluorine compiles and bundles matching
# headers/runtime across supported build images.
ARG LIBFUSE_VERSION=3.18.2
RUN curl -L --fail \
      "https://github.com/libfuse/libfuse/releases/download/fuse-${LIBFUSE_VERSION}/fuse-${LIBFUSE_VERSION}.tar.gz" \
      -o /tmp/libfuse.tar.gz && \
    tar -xf /tmp/libfuse.tar.gz -C /tmp && \
    meson setup /tmp/libfuse-build "/tmp/fuse-${LIBFUSE_VERSION}" \
      --prefix=/usr \
      --libdir=lib/x86_64-linux-gnu \
      -Dexamples=false \
      -Dutils=true \
      -Dtests=false \
      -Duseroot=false && \
    meson compile -C /tmp/libfuse-build && \
    meson install -C /tmp/libfuse-build && \
    ldconfig && \
    test "$(pkg-config --modversion fuse3)" = "${LIBFUSE_VERSION}" && \
    cp "/tmp/fuse-${LIBFUSE_VERSION}/LICENSE" /opt/fluorine-libfuse-LICENSE && \
    cp "/tmp/fuse-${LIBFUSE_VERSION}/GPL2.txt" /opt/fluorine-libfuse-GPL2 && \
    cp "/tmp/fuse-${LIBFUSE_VERSION}/LGPL2.txt" /opt/fluorine-libfuse-LGPL2 && \
    rm -rf /tmp/libfuse.tar.gz "/tmp/fuse-${LIBFUSE_VERSION}" /tmp/libfuse-build

# ── uv (Astral) ──
# Single static binary; manages Python interpreters and packages.
COPY --from=ghcr.io/astral-sh/uv:latest /uv /usr/local/bin/uv

# ── python-build-standalone 3.12 via uv (build-time + bundled runtime) ──
# uv pulls the same PBS tarballs we used to curl by hand.  We install into
# a scratch dir and `mv` the cpython-* tree to /opt/python-bundled so the
# downstream build can rely on a stable, non-symlink path.
ARG PYTHON_VERSION=3.12.13
RUN uv python install --install-dir /opt/uv-tmp "${PYTHON_VERSION}" && \
    PY_DIR="$(find /opt/uv-tmp -maxdepth 1 -type d -name 'cpython-*' | head -n1)" && \
    test -n "${PY_DIR}" && \
    mv "${PY_DIR}" /opt/python-bundled && \
    rm -rf /opt/uv-tmp && \
    /opt/python-bundled/bin/python3 --version
ENV BUILD_PYTHON=/opt/python-bundled/bin/python3
ENV PATH="/opt/python-bundled/bin:${PATH}"

# ── Qt 6.11 via aqtinstall (one-off via uv tool run) ──
RUN uv tool run --from aqtinstall aqt install-qt linux desktop 6.11.1 linux_gcc_64 \
      -m qtwebsockets qtwaylandcompositor qtnetworkauth qttasktree qtimageformats \
      --outputdir /opt/qt6 && \
    ls /opt/qt6/6.11.1/gcc_64/bin/qmake
ARG ICU_LICENSE_SHA256=f3005e195ff74d8812cc1f182a1c446fab678d70a10e3dada497585befee5416
RUN curl -L --fail --retry 3 \
      https://raw.githubusercontent.com/unicode-org/icu/release-73-2/icu4c/LICENSE \
      -o /opt/fluorine-icu-73.2-LICENSE && \
    echo "${ICU_LICENSE_SHA256}  /opt/fluorine-icu-73.2-LICENSE" | \
      sha256sum -c -
ENV Qt6_DIR=/opt/qt6/6.11.1/gcc_64
ENV CMAKE_PREFIX_PATH="/opt/qt6/6.11.1/gcc_64:${CMAKE_PREFIX_PATH}"
ENV PATH="/opt/qt6/6.11.1/gcc_64/bin:${PATH}"
ENV LD_LIBRARY_PATH="/opt/qt6/6.11.1/gcc_64/lib:${LD_LIBRARY_PATH}"

# ── Build-time Python packages ──
# pybind11/sip/etc. for compiling mobase.so; PyQt6 is staged into the distribution.
RUN uv pip install --system --break-system-packages \
      --python /opt/python-bundled/bin/python3 --no-cache \
      pybind11==2.13.6 sip psutil larian-formats==0.8.1 vdf "PyQt6>=6.11,<6.12"

# ── Rust toolchain ──
RUN curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | \
    sh -s -- -y --default-toolchain stable --profile minimal

# ── libloot (LOOT's library) Python bindings — for OpenMW "Sort with LOOT" ──
# No Linux wheel is published, so we build from source with maturin against the
# bundled interpreter and install it into /opt/python-bundled. The wheel/project
# is named "libloot" but the importable module is "loot" (python/Cargo.toml
# [lib] name = "loot"). build-inner.sh stages the resulting loot*.so into the
# runtime site-packages. The whole step is best-effort/optional at runtime: if
# the module is absent the "Sort with LOOT" tool degrades to a clear message.
COPY collect_cargo_licenses.py /usr/local/libexec/collect-cargo-licenses.py
ARG LARIAN_FORMATS_CRATE_SHA256=1913f9bbee52ba773388af75942b90b33f9ee79eef191998593e0f854cf94636
RUN curl -L --fail --retry 3 -A 'cargo/1.0' \
      https://static.crates.io/crates/larian-formats/larian-formats-0.8.1.crate \
      -o /tmp/larian-formats-0.8.1.crate && \
    echo "${LARIAN_FORMATS_CRATE_SHA256}  /tmp/larian-formats-0.8.1.crate" | \
      sha256sum -c - && \
    tar -xf /tmp/larian-formats-0.8.1.crate -C /tmp && \
    cp /usr/share/common-licenses/Apache-2.0 \
      /tmp/larian-formats-0.8.1/LICENSE-APACHE-2.0 && \
    /opt/python-bundled/bin/python3 /usr/local/libexec/collect-cargo-licenses.py \
      --output /opt/python-bundled/share/licenses/larian-formats-cargo \
      /tmp/larian-formats-0.8.1/Cargo.toml && \
    rm -rf /tmp/larian-formats-0.8.1 /tmp/larian-formats-0.8.1.crate
ARG LIBLOOT_VERSION=0.29.6
RUN uv pip install --system --break-system-packages \
      --python /opt/python-bundled/bin/python3 --no-cache "maturin>=1.8,<2.0" && \
    git clone --depth 1 --branch "${LIBLOOT_VERSION}" \
      https://github.com/loot/libloot /tmp/libloot && \
    /opt/python-bundled/bin/python3 -m maturin build --release \
      --manifest-path /tmp/libloot/python/Cargo.toml \
      --interpreter /opt/python-bundled/bin/python3 \
      --compatibility linux --out /tmp/libloot-wheels && \
    uv pip install --system --break-system-packages \
      --python /opt/python-bundled/bin/python3 --no-cache /tmp/libloot-wheels/*.whl && \
    /opt/python-bundled/bin/python3 -c "import loot; print('libloot', loot.libloot_version())" && \
    /opt/python-bundled/bin/python3 /usr/local/libexec/collect-cargo-licenses.py \
      --output /opt/python-bundled/share/licenses/libloot-cargo \
      /tmp/libloot/python/Cargo.toml && \
    rm -rf /tmp/libloot /tmp/libloot-wheels

WORKDIR /build

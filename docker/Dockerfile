FROM ubuntu:25.10

ENV DEBIAN_FRONTEND=noninteractive
ENV LANG=C.UTF-8
ENV RUSTUP_HOME=/opt/rust/rustup
ENV CARGO_HOME=/opt/rust/cargo
ENV PATH="/opt/rust/cargo/bin:${PATH}"

# ── System build tools + all library dependencies ──
RUN apt-get update && apt-get install -y --no-install-recommends \
    # Build essentials
    build-essential cmake ninja-build ccache \
    git curl wget file binutils patchelf pkg-config ca-certificates \
    # Library dependencies
    libboost-all-dev \
    libsqlite3-dev \
    libtinyxml2-dev \
    libfontconfig1-dev \
    libspdlog-dev \
    libfuse3-dev fuse3 \
    liblz4-dev zlib1g-dev libzstd-dev libbz2-dev liblzma-dev \
    libssl-dev libcurl4-openssl-dev \
    libtomlplusplus-dev \
    # Qt 6
    qt6-base-dev qt6-base-dev-tools \
    qt6-webengine-dev \
    libqt6websockets6-dev \
    qt6-wayland \
    # Python
    python3 python3-dev python3-pip python3-venv \
    pybind11-dev \
    python3-pyqt6 \
    # Misc
    icoutils zip unzip \
    && rm -rf /var/lib/apt/lists/*

# ── Python SIP tooling + runtime deps ──
RUN pip3 install --break-system-packages sip psutil vdf || true

# ── Rust toolchain ──
RUN curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | \
    sh -s -- -y --default-toolchain stable --profile minimal

# ── Portable Python runtime (for bundling, not compilation) ──
ARG PORTABLE_PYTHON_VERSION=3.13.9
RUN curl -L --retry 3 -o /tmp/portable-python.zip \
      "https://github.com/bjia56/portable-python/releases/download/cpython-v${PORTABLE_PYTHON_VERSION}-build.0/python-headless-${PORTABLE_PYTHON_VERSION}-linux-x86_64.zip" && \
    mkdir -p /tmp/pp-extract && \
    unzip -q /tmp/portable-python.zip -d /tmp/pp-extract && \
    mv /tmp/pp-extract/python-headless-*/ /opt/portable-python && \
    rm -rf /tmp/portable-python.zip /tmp/pp-extract

# ── Build and install libloot ──
ARG LIBLOOT_REF=master
RUN git clone --depth 1 --branch ${LIBLOOT_REF} \
      https://github.com/loot/libloot.git /tmp/libloot && \
    cmake -S /tmp/libloot/cpp -B /tmp/libloot/build -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_SHARED_LIBS=ON \
      -DLIBLOOT_INSTALL_DOCS=OFF \
      -DBUILD_TESTING=OFF \
      -DCMAKE_INSTALL_PREFIX=/usr/local && \
    cmake --build /tmp/libloot/build --parallel && \
    cmake --install /tmp/libloot/build && \
    # Create pkg-config metadata
    mkdir -p /usr/local/lib/pkgconfig && \
    printf '%s\n' \
      'prefix=/usr/local' \
      'exec_prefix=${prefix}' \
      'libdir=${prefix}/lib' \
      'includedir=${prefix}/include' \
      '' \
      'Name: libloot' \
      'Description: LOOT C++ API library' \
      'Version: 0.29.0' \
      'Libs: -L${libdir} -lloot' \
      'Cflags: -I${includedir}' \
      > /usr/local/lib/pkgconfig/libloot.pc && \
    ldconfig && \
    rm -rf /tmp/libloot

# ── Pre-download linuxdeploy tooling ──
RUN mkdir -p /opt/linuxdeploy && \
    curl -L --retry 3 -o /opt/linuxdeploy/linuxdeploy-x86_64.AppImage \
      https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage && \
    curl -L --retry 3 -o /opt/linuxdeploy/linuxdeploy-plugin-qt-x86_64.AppImage \
      https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage && \
    chmod +x /opt/linuxdeploy/linuxdeploy-x86_64.AppImage \
             /opt/linuxdeploy/linuxdeploy-plugin-qt-x86_64.AppImage && \
    # Extract Qt plugin (avoids FUSE requirement at runtime)
    cd /opt/linuxdeploy && \
    ./linuxdeploy-plugin-qt-x86_64.AppImage --appimage-extract >/dev/null && \
    mv squashfs-root linuxdeploy-plugin-qt.AppDir && \
    chmod +x linuxdeploy-plugin-qt.AppDir/AppRun && \
    ln -sf /opt/linuxdeploy/linuxdeploy-plugin-qt.AppDir/AppRun \
           /opt/linuxdeploy/linuxdeploy-plugin-qt && \
    chmod -x /opt/linuxdeploy/linuxdeploy-plugin-qt-x86_64.AppImage

WORKDIR /build

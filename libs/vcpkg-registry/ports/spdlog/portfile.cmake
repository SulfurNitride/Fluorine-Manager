vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO gabime/spdlog
    REF "v${VERSION}"
    SHA512 21C35F4091850EA3A0CD6A24867E06E943DF70D76CD5A7EC0B15A33E0E9E0CC3584ED7930E1AC6F347E7E06F0E002D0E759884EAF05310014E24EA0E0419FCC4
    HEAD_REF v1.x
)

string(COMPARE EQUAL "${VCPKG_LIBRARY_LINKAGE}" "dynamic" SPDLOG_BUILD_SHARED)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        ${FEATURE_OPTIONS}
        -DSPDLOG_FMT_EXTERNAL=OFF
        -DSPDLOG_USE_STD_FORMAT=ON
        -DSPDLOG_INSTALL=ON
        -DSPDLOG_BUILD_SHARED=${SPDLOG_BUILD_SHARED}
        -DSPDLOG_WCHAR_FILENAMES=OFF
        -DSPDLOG_BUILD_EXAMPLE=OFF
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/spdlog)
vcpkg_fixup_pkgconfig()
vcpkg_copy_pdbs()

# add support for integration other than cmake
vcpkg_replace_string(${CURRENT_PACKAGES_DIR}/include/spdlog/tweakme.h
    "// #define SPDLOG_USE_STD_FORMAT"
    "#ifndef SPDLOG_USE_STD_FORMAT\n#define SPDLOG_USE_STD_FORMAT\n#endif"
)
vcpkg_replace_string(${CURRENT_PACKAGES_DIR}/include/spdlog/tweakme.h
    "// #define SPDLOG_WCHAR_TO_UTF8_SUPPORT"
    "#ifndef SPDLOG_WCHAR_TO_UTF8_SUPPORT\n#define SPDLOG_WCHAR_TO_UTF8_SUPPORT\n#endif"
)

file(REMOVE_RECURSE
    "${CURRENT_PACKAGES_DIR}/include/spdlog/fmt/bundled"
    "${CURRENT_PACKAGES_DIR}/debug/include"
    "${CURRENT_PACKAGES_DIR}/debug/share"
)

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")

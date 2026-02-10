vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO asmjit/asmjit
    REF fb9f82cb61df36aa513d054e748dc6769045f33e
    SHA512 937a1ea1855d7eef53e6afc3401dd015e5de26d174dc667f8be4580a2d8388a703fff4298e4e2ca9ea490c5053197f33d3b010456985243931a1c0a6845926cc
    HEAD_REF master
    PATCHES
        asmjit.patch
)

set(VCPKG_LIBRARY_LINKAGE static)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DASMJIT_STATIC=TRUE
)

vcpkg_cmake_install()
vcpkg_copy_pdbs()

vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/asmjit)

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

# vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/include/asmjit/core/api-config.h"
#     "#if !defined(ASMJIT_STATIC)"
#     "#if 0"
# )

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE.md")

set(VCPKG_LIBRARY_LINKAGE dynamic)
set(VCPKG_POLICY_DLLS_IN_STATIC_LIBRARY enabled)

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO ModOrganizer2/modorganizer-archive
    REF "${VERSION}"
    SHA512 A6AE266AB6EF2AB3C3E20899EAC5D89474D5A4F9BC3D7A185563D4831DD94AB4F96B2991B06A2151549924709CBCD7BE29E610BF5051FB81BCCEAD2AB2A12BC0
    HEAD_REF master
)

vcpkg_cmake_configure(SOURCE_PATH "${SOURCE_PATH}")
vcpkg_cmake_install()
vcpkg_copy_pdbs()

vcpkg_cmake_config_fixup(PACKAGE_NAME "mo2-archive" CONFIG_PATH "lib/cmake/mo2-archive")

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")

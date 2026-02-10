vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO ModOrganizer2/modorganizer-lootcli
    REF ${VERSION}
    SHA512 07A29569D2577A8ED0D7C66D0907EAAF73F3C3E3A2BF5882CB435B69FC797186E233AF89B5A876AAE8838AA40A9C75BFFE8DBCBDF12485C7ED6206C3B6C6A73B
    HEAD_REF master
)

vcpkg_cmake_configure(SOURCE_PATH "${SOURCE_PATH}")
vcpkg_cmake_install()

vcpkg_cmake_config_fixup(PACKAGE_NAME "mo2-lootcli-header" CONFIG_PATH "lib/cmake/mo2-lootcli-header")

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

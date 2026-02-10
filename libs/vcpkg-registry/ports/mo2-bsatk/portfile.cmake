vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO ModOrganizer2/modorganizer-bsatk
    REF "${VERSION}"
    SHA512 4062EC65C20ECE77EBBC90E206D7BC7D079BDC411C54E4D6E8F5AA3047F9C13CC08771A103527FB8DEE3814116904A23FF59822FB8C268E3EFC4ABCA2BF2B2A9
    HEAD_REF master
)

vcpkg_cmake_configure(SOURCE_PATH "${SOURCE_PATH}")
vcpkg_cmake_install()
vcpkg_copy_pdbs()

vcpkg_cmake_config_fixup(PACKAGE_NAME "mo2-bsatk" CONFIG_PATH "lib/cmake/mo2-bsatk")

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

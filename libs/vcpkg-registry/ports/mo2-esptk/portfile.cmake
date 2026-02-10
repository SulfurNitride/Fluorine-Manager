vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO ModOrganizer2/modorganizer-esptk
    REF "${VERSION}"
    SHA512 FCC0AFCB07E32DC1C6B310D6B277FB4B472702BA9BCCBBB604DBBC68566D3CE8BEF9EB4B17D52534DC1D75925879555FBBDB0080B797885A22F0070461C28A11
    HEAD_REF master
)

vcpkg_cmake_configure(SOURCE_PATH "${SOURCE_PATH}")
vcpkg_cmake_install()
vcpkg_copy_pdbs()

vcpkg_cmake_config_fixup(PACKAGE_NAME "mo2-esptk" CONFIG_PATH "lib/cmake/mo2-esptk")

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

set(VCPKG_POLICY_DLLS_IN_STATIC_LIBRARY enabled)

vcpkg_download_distfile(ARCHIVE
    URLS "https://github.com/ModOrganizer2/libbsarch/releases/download/${VERSION}/libbsarch_${VERSION}.7z"
    FILENAME "libbsarch-${VERSION}.7z"
    SHA512 88ACA64F8467234CA7B51031372387F0A0927F962CF44D41D0842BB632A92BECEAD0BFDE4A61C10FB45531F0FD85CB5C59C67749635E0E7905A2BDF75B601B37
)

vcpkg_extract_source_archive_ex(
    OUT_SOURCE_PATH SOURCE_PATH
    ARCHIVE ${ARCHIVE}
    NO_REMOVE_ONE_LEVEL
)

vcpkg_download_distfile(LICENSE
    URLS "https://raw.githubusercontent.com/ModOrganizer2/libbsarch/master/LICENSE"
    FILENAME "copyright"
    SHA512 0ABC51233EE794D26C1E9F5A15E7FB3DED1E832F1A090C859F9F2FF8D15B56DA023B1CCC98099C96B214C18A3CD6971D620267A19202E56551B9C57BF086A87A
)

file(INSTALL ${SOURCE_PATH}/include DESTINATION ${CURRENT_PACKAGES_DIR})
file(INSTALL ${SOURCE_PATH}/bin DESTINATION ${CURRENT_PACKAGES_DIR})
file(INSTALL ${SOURCE_PATH}/lib DESTINATION ${CURRENT_PACKAGES_DIR})

file(INSTALL ${SOURCE_PATH}/bin DESTINATION ${CURRENT_PACKAGES_DIR}/debug)
file(INSTALL ${SOURCE_PATH}/lib DESTINATION ${CURRENT_PACKAGES_DIR}/debug)

vcpkg_cmake_config_fixup(CONFIG_PATH "lib/cmake/${PORT}")
vcpkg_install_copyright(FILE_LIST "${LICENSE}")

# remove release from debug and debug from release (mainly to silence VCPKG warnings)
file(REMOVE
    "${CURRENT_PACKAGES_DIR}/bin/libbsarchd.dll"
    "${CURRENT_PACKAGES_DIR}/lib/libbsarchd.lib"
    "${CURRENT_PACKAGES_DIR}/lib/libbsarch_OOPd.lib"

    "${CURRENT_PACKAGES_DIR}/debug/bin/libbsarch.dll"
    "${CURRENT_PACKAGES_DIR}/debug/lib/libbsarch.lib"
    "${CURRENT_PACKAGES_DIR}/debug/lib/libbsarch_OOP.lib"
)

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/share")

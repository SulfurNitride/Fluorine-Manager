cmake_minimum_required(VERSION 3.16)

find_package(Boost CONFIG REQUIRED COMPONENTS filesystem)

add_executable(usvfs_proxy main.cpp version.rc)
target_link_libraries(usvfs_proxy PRIVATE usvfs_dll shared usvfs_helper)
set_target_properties(usvfs_proxy
    PROPERTIES
    RUNTIME_OUTPUT_NAME usvfs_proxy${ARCH_POSTFIX}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${USVFS_BINDIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${USVFS_BINDIR}
)

install(TARGETS usvfs_proxy EXPORT usvfs${ARCH_POSTFIX}Targets)
install(FILES $<TARGET_PDB_FILE:usvfs_proxy> DESTINATION pdb OPTIONAL)

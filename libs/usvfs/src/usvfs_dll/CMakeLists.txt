cmake_minimum_required(VERSION 3.16)

include(GNUInstallDirs)

find_package(Boost CONFIG REQUIRED COMPONENTS thread)
find_package(spdlog CONFIG REQUIRED)

file(GLOB sources "*.cpp" "*.h")
source_group("" FILES ${sources})

file(GLOB hooks "hooks/*.cpp" "hooks/*.h")
source_group("dlls" FILES ${hooks})

add_library(usvfs_dll SHARED)
target_sources(usvfs_dll
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include/usvfs/usvfsparametersprivate.h
        ${sources}
        ${hooks}
        version.rc
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS ${PROJECT_SOURCE_DIR}/include
        FILES
        ${PROJECT_SOURCE_DIR}/include/usvfs/dllimport.h
        ${PROJECT_SOURCE_DIR}/include/usvfs/logging.h
        ${PROJECT_SOURCE_DIR}/include/usvfs/sharedparameters.h
        ${PROJECT_SOURCE_DIR}/include/usvfs/usvfs_version.h
        ${PROJECT_SOURCE_DIR}/include/usvfs/usvfs.h
        ${PROJECT_SOURCE_DIR}/include/usvfs/usvfsparameters.h
)
target_link_libraries(usvfs_dll
    PRIVATE shared thooklib usvfs_helper
    Boost::thread spdlog::spdlog_header_only
    Shlwapi)
target_compile_definitions(usvfs_dll PRIVATE BUILDING_USVFS_DLL)
set_target_properties(usvfs_dll
    PROPERTIES
    ARCHIVE_OUTPUT_NAME usvfs${ARCH_POSTFIX}
    ARCHIVE_OUTPUT_DIRECTORY_DEBUG ${USVFS_LIBDIR}
    ARCHIVE_OUTPUT_DIRECTORY_RELEASE ${USVFS_LIBDIR}
    LIBRARY_OUTPUT_NAME usvfs${ARCH_POSTFIX}
    LIBRARY_OUTPUT_DIRECTORY_DEBUG ${USVFS_LIBDIR}
    LIBRARY_OUTPUT_DIRECTORY_RELEASE ${USVFS_LIBDIR}
    PDB_OUTPUT_DIRECTORY_DEBUG ${USVFS_LIBDIR}
    PDB_OUTPUT_DIRECTORY_RELEASE ${USVFS_LIBDIR}
    RUNTIME_OUTPUT_NAME usvfs${ARCH_POSTFIX}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${USVFS_LIBDIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${USVFS_LIBDIR})

install(TARGETS usvfs_dll EXPORT usvfs${ARCH_POSTFIX}Targets FILE_SET HEADERS)
install(FILES $<TARGET_PDB_FILE:usvfs_dll> DESTINATION pdb OPTIONAL)
install(EXPORT usvfs${ARCH_POSTFIX}Targets
    FILE usvfs${ARCH_POSTFIX}Targets.cmake
    NAMESPACE usvfs${ARCH_POSTFIX}::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/usvfs
)

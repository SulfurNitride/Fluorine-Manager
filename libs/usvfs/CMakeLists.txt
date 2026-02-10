cmake_minimum_required(VERSION 3.16)

include(CMakePackageConfigHelpers)

project(usvfs)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set(CMAKE_VS_INCLUDE_INSTALL_TO_DEFAULT_BUILD 1)
set(CMAKE_CXX_STANDARD 20)

set(USVFS_BINDIR ${CMAKE_CURRENT_LIST_DIR}/bin)
set(USVFS_LIBDIR ${CMAKE_CURRENT_LIST_DIR}/lib)

if (MSVC)
    # /Zi generate PDBs
    # /Gy enable function-level linking
    # /Oi enable intrinsic function
    add_compile_options("$<$<NOT:$<CONFIG:DEBUG>>:/Zi;/Gy;/Oi>")

    # /OPT:ICF enable COMDAT folding
    # /DEBUG:FULL generate debug info (PDB)
    # /OPT:REF enable references (PDB)
    add_link_options("$<$<NOT:$<CONFIG:DEBUG>>:/OPT:ICF;/DEBUG:FULL;/OPT:REF>")
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(ARCH_POSTFIX _x86)
else()
    set(ARCH_POSTFIX _x64)
endif()

add_subdirectory(src/shared)

add_subdirectory(src/thooklib)
add_subdirectory(src/tinjectlib)
add_subdirectory(src/usvfs_helper)

add_subdirectory(src/usvfs_dll)
add_subdirectory(src/usvfs_proxy)

configure_package_config_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake/config.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/usvfsConfig.cmake"
  INSTALL_DESTINATION "lib/cmake/usvfs"
  NO_SET_AND_CHECK_MACRO
  NO_CHECK_REQUIRED_COMPONENTS_MACRO
)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/include/usvfs/usvfs_version.h" usvfs_version)
string(REGEX MATCH "USVFS_VERSION_MAJOR ([0-9]*)" _ ${usvfs_version})
set(usvfs_version_major ${CMAKE_MATCH_1})
string(REGEX MATCH "USVFS_VERSION_MINOR ([0-9]*)" _ ${usvfs_version})
set(usvfs_version_minor ${CMAKE_MATCH_1})
string(REGEX MATCH "USVFS_VERSION_BUILD ([0-9]*)" _ ${usvfs_version})
set(usvfs_version_build ${CMAKE_MATCH_1})
string(REGEX MATCH "USVFS_VERSION_REVISION ([0-9]*)" _ ${usvfs_version})
set(usvfs_version_revision ${CMAKE_MATCH_1})

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/usvfsConfigVersion.cmake"
  VERSION "${usvfs_version_major}.${usvfs_version_minor}.${usvfs_version_build}.${usvfs_version_revision}"
  COMPATIBILITY AnyNewerVersion
  ARCH_INDEPENDENT
)

install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/usvfsConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/usvfsConfigVersion.cmake
  DESTINATION lib/cmake/usvfs
)

if (BUILD_TESTING)
    enable_testing()
    set(USVFS_TEST_BINDIR ${CMAKE_CURRENT_LIST_DIR}/test/bin)
    add_subdirectory(test)
endif()

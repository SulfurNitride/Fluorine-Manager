cmake_minimum_required(VERSION 3.16)

include(CMakePackageConfigHelpers)

project(lootcli)

add_subdirectory(src)

# install the header helper
configure_package_config_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake/config.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/mo2-lootcli-header-config.cmake"
  INSTALL_DESTINATION "lib/cmake/mo2-lootcli-header"
  NO_SET_AND_CHECK_MACRO
  NO_CHECK_REQUIRED_COMPONENTS_MACRO
)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/src/version.h" lootcli_version)
string(REGEX MATCH "#define LOOTCLI_VERSION_STRING[ \t]*\"([0-9.]+)\"" _ ${lootcli_version})
set(lootcli_version ${CMAKE_MATCH_1})

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/mo2-lootcli-header-config-version.cmake"
  VERSION "${lootcli_version}"
  COMPATIBILITY AnyNewerVersion
  ARCH_INDEPENDENT
)

install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/mo2-lootcli-header-config.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/mo2-lootcli-header-config-version.cmake
  DESTINATION lib/cmake/mo2-lootcli-header
)

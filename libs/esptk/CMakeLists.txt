cmake_minimum_required(VERSION 3.16)

include(CMakePackageConfigHelpers)

project(esptk)

add_subdirectory(src)

configure_package_config_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake/config.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/mo2-esptk-config.cmake"
  INSTALL_DESTINATION "lib/cmake/mo2-esptk"
  NO_SET_AND_CHECK_MACRO
  NO_CHECK_REQUIRED_COMPONENTS_MACRO
)

include(${CMAKE_CURRENT_SOURCE_DIR}/Version.cmake)
set(ESPTK_VERSION
  ${ESPTK_MAJOR_VERSION}.${ESPTK_MINOR_VERSION}.${ESPTK_PATCH_VERSION})

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/mo2-esptk-config-version.cmake"
  VERSION "${ESPTK_VERSION}"
  COMPATIBILITY AnyNewerVersion
)

install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/mo2-esptk-config.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/mo2-esptk-config-version.cmake
  DESTINATION lib/cmake/mo2-esptk
)

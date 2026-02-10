cmake_minimum_required(VERSION 3.16)

include(CMakePackageConfigHelpers)

project(uibase VERSION 2.5.3)

# Standalone build - bypass mo2-cmake
add_subdirectory(src)

configure_package_config_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake/config.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/mo2-uibase-config.cmake"
  INSTALL_DESTINATION "lib/cmake/mo2-uibase"
  NO_SET_AND_CHECK_MACRO
  NO_CHECK_REQUIRED_COMPONENTS_MACRO
)

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/mo2-uibase-config-version.cmake"
  VERSION "${PROJECT_VERSION}"
  COMPATIBILITY AnyNewerVersion
  ARCH_INDEPENDENT
)

install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/mo2-uibase-config.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/mo2-uibase-config-version.cmake
  DESTINATION lib/cmake/mo2-uibase
)

set(BUILD_TESTING ${BUILD_TESTING} CACHE BOOL "build tests for uibase")
if (BUILD_TESTING)
	enable_testing()
	add_subdirectory(tests)
endif()

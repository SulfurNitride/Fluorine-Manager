
cmake_minimum_required(VERSION 3.16)

file(GLOB installer_bundle_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/*.h
  ${CMAKE_CURRENT_SOURCE_DIR}/*.ui
  ${CMAKE_CURRENT_SOURCE_DIR}/*.qrc
)

add_library(installer_bundle SHARED ${installer_bundle_SOURCES})
mo2_configure_plugin(installer_bundle NO_SOURCES WARNINGS OFF)
target_link_libraries(installer_bundle PRIVATE mo2::uibase)
mo2_install_plugin(installer_bundle)

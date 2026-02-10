cmake_minimum_required(VERSION 3.16)

file(GLOB INSTALLER_FOMOD_SOURCES
  CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/*.h
  ${CMAKE_CURRENT_SOURCE_DIR}/*.ui
  ${CMAKE_CURRENT_SOURCE_DIR}/*.qrc
)

add_library(installer_fomod SHARED ${INSTALLER_FOMOD_SOURCES})
mo2_configure_plugin(installer_fomod WARNINGS OFF)
target_include_directories(installer_fomod PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_BINARY_DIR}
)
target_link_libraries(installer_fomod PRIVATE mo2::uibase Qt6::Widgets)
mo2_install_plugin(installer_fomod)

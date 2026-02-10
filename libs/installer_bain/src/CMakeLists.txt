cmake_minimum_required(VERSION 3.16)

file(GLOB installer_bain_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/*.h
  ${CMAKE_CURRENT_SOURCE_DIR}/*.ui
  ${CMAKE_CURRENT_SOURCE_DIR}/*.qrc
)

add_library(installer_bain SHARED ${installer_bain_SOURCES})
mo2_configure_plugin(installer_bain NO_SOURCES WARNINGS OFF)
target_link_libraries(installer_bain PRIVATE mo2::uibase)
mo2_install_plugin(installer_bain)

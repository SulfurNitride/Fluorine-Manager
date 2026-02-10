cmake_minimum_required(VERSION 3.16)

file(GLOB installer_quick_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/*.h
  ${CMAKE_CURRENT_SOURCE_DIR}/*.ui
  ${CMAKE_CURRENT_SOURCE_DIR}/*.qrc
)

add_library(installer_quick SHARED ${installer_quick_SOURCES})
mo2_configure_plugin(installer_quick NO_SOURCES WARNINGS 4)
target_link_libraries(installer_quick PRIVATE mo2::uibase)
mo2_install_plugin(installer_quick)

cmake_minimum_required(VERSION 3.16)

file(GLOB installer_manual_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/*.h
  ${CMAKE_CURRENT_SOURCE_DIR}/*.ui
  ${CMAKE_CURRENT_SOURCE_DIR}/*.qrc
)

add_library(installer_manual SHARED ${installer_manual_SOURCES})
mo2_configure_plugin(installer_manual NO_SOURCES WARNINGS 4)
target_include_directories(installer_manual PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_BINARY_DIR}
)
target_link_libraries(installer_manual PRIVATE mo2::uibase)
mo2_install_plugin(installer_manual)

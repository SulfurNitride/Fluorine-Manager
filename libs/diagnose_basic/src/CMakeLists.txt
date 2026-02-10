cmake_minimum_required(VERSION 3.16)

file(GLOB diagnose_basic_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/*.h
  ${CMAKE_CURRENT_SOURCE_DIR}/*.ui
  ${CMAKE_CURRENT_SOURCE_DIR}/*.qrc
)

add_library(diagnose_basic SHARED ${diagnose_basic_SOURCES})
mo2_configure_plugin(diagnose_basic NO_SOURCES WARNINGS OFF)
target_link_libraries(diagnose_basic PRIVATE mo2::uibase)
mo2_install_plugin(diagnose_basic)

cmake_minimum_required(VERSION 3.16)

file(GLOB check_fnis_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/*.h
  ${CMAKE_CURRENT_SOURCE_DIR}/*.ui
  ${CMAKE_CURRENT_SOURCE_DIR}/*.qrc
)

add_library(check_fnis SHARED ${check_fnis_SOURCES})
mo2_configure_plugin(check_fnis NO_SOURCES WARNINGS OFF)
target_link_libraries(check_fnis PRIVATE mo2::uibase)

mo2_install_plugin(check_fnis)

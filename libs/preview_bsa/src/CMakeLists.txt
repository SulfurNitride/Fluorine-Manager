cmake_minimum_required(VERSION 3.16)

file(GLOB preview_bsa_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/*.h
  ${CMAKE_CURRENT_SOURCE_DIR}/*.ui
  ${CMAKE_CURRENT_SOURCE_DIR}/*.qrc
)

add_library(preview_bsa SHARED ${preview_bsa_SOURCES})
mo2_configure_plugin(preview_bsa NO_SOURCES WARNINGS 4)
target_link_libraries(preview_bsa PRIVATE mo2::uibase mo2::bsatk)
mo2_install_plugin(preview_bsa)

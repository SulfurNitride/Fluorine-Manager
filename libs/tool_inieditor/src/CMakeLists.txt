cmake_minimum_required(VERSION 3.16)

file(GLOB inieditor_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/*.h
  ${CMAKE_CURRENT_SOURCE_DIR}/*.ui
  ${CMAKE_CURRENT_SOURCE_DIR}/*.qrc
)

add_library(inieditor SHARED ${inieditor_SOURCES})
mo2_configure_plugin(inieditor NO_SOURCES WARNINGS 4)
target_link_libraries(inieditor PRIVATE mo2::uibase)
mo2_install_plugin(inieditor)

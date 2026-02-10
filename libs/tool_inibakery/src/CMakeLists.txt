cmake_minimum_required(VERSION 3.16)

file(GLOB inibakery_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/*.h
  ${CMAKE_CURRENT_SOURCE_DIR}/*.ui
  ${CMAKE_CURRENT_SOURCE_DIR}/*.qrc
)

add_library(inibakery SHARED ${inibakery_SOURCES})
mo2_configure_plugin(inibakery NO_SOURCES WARNINGS 4)
target_link_libraries(inibakery PRIVATE mo2::uibase)
mo2_install_plugin(inibakery)

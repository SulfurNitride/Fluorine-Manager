cmake_minimum_required(VERSION 3.16)

file(GLOB bsa_extractor_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/*.h
  ${CMAKE_CURRENT_SOURCE_DIR}/*.ui
  ${CMAKE_CURRENT_SOURCE_DIR}/*.qrc
)

add_library(bsa_extractor SHARED ${bsa_extractor_SOURCES})
mo2_configure_plugin(bsa_extractor NO_SOURCES WARNINGS OFF)
target_link_libraries(bsa_extractor PRIVATE mo2::uibase mo2::bsatk)
mo2_install_plugin(bsa_extractor)

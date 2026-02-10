cmake_minimum_required(VERSION 3.16)

project(uibase-tests-cmake)

find_package(mo2-uibase CONFIG REQUIRED)

add_library(plugin SHARED)
target_sources(plugin PRIVATE plugin.cpp)
target_link_libraries(plugin PRIVATE mo2::uibase)
set_target_properties(plugin PROPERTIES CXX_STANDARD 20)

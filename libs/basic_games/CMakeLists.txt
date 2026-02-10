cmake_minimum_required(VERSION 3.16)

project(basic_games LANGUAGES NONE)

find_package(mo2-cmake CONFIG REQUIRED)

add_custom_target(basic_games ALL)
mo2_configure_python(basic_games MODULE TRANSLATIONS OFF)

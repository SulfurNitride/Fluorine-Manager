cmake_minimum_required(VERSION 3.16)

find_package(mo2-cmake CONFIG REQUIRED)

add_custom_target(Form43Checker ALL)
mo2_configure_python(Form43Checker SIMPLE)

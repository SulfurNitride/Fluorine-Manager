cmake_minimum_required(VERSION 3.16)

find_package(mo2-cmake CONFIG REQUIRED)

add_custom_target(DDSPreview ALL)
mo2_configure_python(DDSPreview SIMPLE)

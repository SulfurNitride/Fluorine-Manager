cmake_minimum_required(VERSION 3.16)

find_package(mo2-cmake CONFIG REQUIRED)
find_package(mo2-uibase CONFIG REQUIRED)

add_library(installer_fomod_csharp SHARED)
mo2_configure_plugin(installer_fomod_csharp WARNINGS OFF CLI ON)
target_link_libraries(installer_fomod_csharp PRIVATE mo2::uibase)
mo2_install_plugin(installer_fomod_csharp)

set_target_properties(installer_fomod_csharp PROPERTIES CXX_STANDARD 20)

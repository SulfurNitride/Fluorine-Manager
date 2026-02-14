cmake_minimum_required(VERSION 3.16)
include(FetchContent)

file(GLOB_RECURSE INSTALLER_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/*.ui
    ${CMAKE_CURRENT_SOURCE_DIR}/*.qrc
)
file(GLOB SHARE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/../share/**/*.cpp")

add_library(fomod_plus_installer SHARED ${INSTALLER_SOURCES} ${SHARE_SOURCES})

target_include_directories(
        fomod_plus_installer
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../share
        ${MO2_UIBASE_INCLUDE_DIRS}
        ${MO2_ARCHIVE_INCLUDE_DIRS}
        ${CMAKE_CURRENT_SOURCE_DIR}
)

FetchContent_Declare(
        pugixml
        GIT_REPOSITORY https://github.com/zeux/pugixml
        GIT_TAG v1.14
        PATCH_COMMAND ${CMAKE_COMMAND}
                -DPUGIXML_CMAKELISTS=<SOURCE_DIR>/CMakeLists.txt
                -P ${CMAKE_CURRENT_LIST_DIR}/../cmake/patch_pugixml.cmake
)

FetchContent_Declare(json URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(pugixml json)

set(project_type plugin)
project(fomod_plus_installer)

target_link_libraries(fomod_plus_installer PRIVATE mo2::uibase pugixml nlohmann_json::nlohmann_json)
if (WIN32)
    target_link_libraries(fomod_plus_installer PRIVATE dbghelp)
endif ()
mo2_configure_plugin(fomod_plus_installer NO_SOURCES WARNINGS OFF PRIVATE_DEPENDS)

if (MSVC)
    target_compile_options(fomod_plus_installer PRIVATE $<$<CONFIG:RelWithDebInfo>:/Zi>)
    target_link_options(fomod_plus_installer PRIVATE $<$<CONFIG:RelWithDebInfo>:/DEBUG>)
    install(FILES $<TARGET_PDB_FILE:fomod_plus_installer>
            DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO}
            CONFIGURATIONS RelWithDebInfo
            OPTIONAL)
endif ()

mo2_install_target(fomod_plus_installer)

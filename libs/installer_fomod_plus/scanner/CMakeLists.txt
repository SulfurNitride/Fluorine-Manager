cmake_minimum_required(VERSION 3.16)
project(fomod_plus_scanner)

include(FetchContent)
set(project_type plugin)

file(GLOB_RECURSE SCANNER_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/*.ui
    ${CMAKE_CURRENT_SOURCE_DIR}/*.qrc
)

add_library(fomod_plus_scanner SHARED ${SCANNER_SOURCES})

FetchContent_Declare(json URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_Declare(pugixml GIT_REPOSITORY https://github.com/zeux/pugixml GIT_TAG v1.14)
FetchContent_MakeAvailable(pugixml json)

target_include_directories(
        fomod_plus_scanner
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../share
        ${CMAKE_CURRENT_SOURCE_DIR}/../share/FOMODData
        ${MO2_UIBASE_INCLUDE_DIRS}
        ${MO2_ARCHIVE_INCLUDE_DIRS}
)

if (MSVC)
    target_compile_options(
            fomod_plus_scanner
            PRIVATE
            /bigobj
            /W4
            /WX
            /wd4201
            /wd4458
    )
endif ()

target_link_libraries(fomod_plus_scanner PRIVATE mo2::uibase mo2::archive pugixml nlohmann_json::nlohmann_json)
mo2_configure_plugin(fomod_plus_scanner NO_SOURCES WARNINGS OFF PRIVATE_DEPENDS archive)

if (MSVC)
    target_compile_options(fomod_plus_scanner PRIVATE $<$<CONFIG:RelWithDebInfo>:/Zi>)
    target_link_options(fomod_plus_scanner PRIVATE $<$<CONFIG:RelWithDebInfo>:/DEBUG>)
    install(FILES $<TARGET_PDB_FILE:fomod_plus_scanner>
            DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO}
            CONFIGURATIONS RelWithDebInfo
            OPTIONAL)
endif ()

mo2_install_target(fomod_plus_scanner)

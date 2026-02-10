cmake_minimum_required(VERSION 3.16)

find_package(Qt6 COMPONENTS Widgets REQUIRED)

# Try cmake config first, fall back to pkg-config
find_package(lz4 CONFIG QUIET)
if(NOT lz4_FOUND)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LZ4 REQUIRED IMPORTED_TARGET liblz4)
endif()

set(CMAKE_AUTOMOC ON)

add_library(game_creation STATIC
    creationgameplugins.h
    creationgameplugins.cpp
)

target_link_libraries(game_creation
    PUBLIC game_gamebryo
    PRIVATE $<IF:$<TARGET_EXISTS:lz4::lz4>,lz4::lz4,PkgConfig::LZ4>
)

target_include_directories(game_creation PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Keep uibase bare includes after system headers to avoid strings.h collision.
target_compile_options(game_creation PRIVATE
    "-idirafter" "${CMAKE_SOURCE_DIR}/libs/uibase/include/uibase"
)

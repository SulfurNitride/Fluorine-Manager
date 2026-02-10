cmake_minimum_required(VERSION 3.16)

find_package(Qt6 COMPONENTS Widgets REQUIRED)

set(CMAKE_AUTOMOC ON)

add_library(game_skyrimse SHARED
    gameskyrimse.cpp
    gameskyrimse.h
    skyrimsedataarchives.cpp
    skyrimsedataarchives.h
    skyrimsemoddatachecker.h
    skyrimsemoddatacontent.h
    skyrimsesavegame.cpp
    skyrimsesavegame.h
    skyrimsescriptextender.cpp
    skyrimsescriptextender.h
    skyrimseunmanagedmods.cpp
    skyrimseunmanagedmods.h
)

target_link_libraries(game_skyrimse PRIVATE game_creation)

# Keep uibase bare includes after system headers to avoid strings.h collision.
target_compile_options(game_skyrimse PRIVATE
    "-idirafter" "${CMAKE_SOURCE_DIR}/libs/uibase/include/uibase"
)

set_property(TARGET game_skyrimse APPEND PROPERTY AUTOMOC_MOC_OPTIONS
    "-I${CMAKE_SOURCE_DIR}/libs/uibase/include/uibase"
)

# Install the .so plugin next to the organizer binary
install(TARGETS game_skyrimse
    LIBRARY DESTINATION plugins
)

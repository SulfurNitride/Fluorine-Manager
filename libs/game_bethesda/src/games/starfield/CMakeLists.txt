cmake_minimum_required(VERSION 3.16)

add_library(game_starfield SHARED
	gamestarfield.cpp
	gamestarfield.h
	starfieldbsainvalidation.cpp
	starfieldbsainvalidation.h
	starfielddataarchives.cpp
	starfielddataarchives.h
	starfieldgameplugins.cpp
	starfieldgameplugins.h
	starfieldmoddatachecker.h
	starfieldmoddatacontent.h
	starfieldsavegame.cpp
	starfieldsavegame.h
	starfieldscriptextender.cpp
	starfieldscriptextender.h
	starfieldunmanagedmods.cpp
	starfieldunmanagedmods.h
)
mo2_configure_plugin(game_starfield NO_SOURCES WARNINGS 4)
mo2_default_source_group()
target_link_libraries(game_starfield PRIVATE game_creation)
mo2_install_plugin(game_starfield)

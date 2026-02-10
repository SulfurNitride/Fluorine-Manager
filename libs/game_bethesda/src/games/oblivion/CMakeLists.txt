cmake_minimum_required(VERSION 3.16)

add_library(game_oblivion SHARED
	gameoblivion.cpp
	gameoblivion.h
	oblivionbsainvalidation.cpp
	oblivionbsainvalidation.h
	obliviondataarchives.cpp
	obliviondataarchives.h
	oblivionmoddatachecker.cpp
	oblivionmoddatachecker.h
	oblivionmoddatacontent.h
	oblivionsavegame.cpp
	oblivionsavegame.h
	oblivionscriptextender.cpp
	oblivionscriptextender.h
)
mo2_configure_plugin(game_oblivion NO_SOURCES WARNINGS 4)
mo2_default_source_group()
target_link_libraries(game_oblivion PRIVATE game_gamebryo)
mo2_install_plugin(game_oblivion)

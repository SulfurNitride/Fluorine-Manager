cmake_minimum_required(VERSION 3.16)

add_library(game_enderal SHARED
	enderalbsainvalidation.cpp
	enderalbsainvalidation.h
	enderaldataarchives.cpp
	enderaldataarchives.h
	enderalgameplugins.cpp
	enderalgameplugins.h
	enderallocalsavegames.cpp
	enderallocalsavegames.h
	enderalmoddatachecker.h
	enderalmoddatacontent.h
	enderalsavegame.cpp
	enderalsavegame.h
	enderalscriptextender.cpp
	enderalscriptextender.h
	gameenderal.cpp
	gameenderal.h
)
mo2_configure_plugin(game_enderal NO_SOURCES WARNINGS 4)
mo2_default_source_group()
target_link_libraries(game_enderal PRIVATE game_creation)
mo2_install_plugin(game_enderal)

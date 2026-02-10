cmake_minimum_required(VERSION 3.16)

add_library(game_skyrimvr SHARED
	gameskyrimvr.cpp
	gameskyrimvr.h
	skyrimvrdataarchives.cpp
	skyrimvrdataarchives.h
	skyrimvrgameplugins.cpp
	skyrimvrgameplugins.h
	skyrimvrmoddatachecker.h
	skyrimvrmoddatacontent.h
	skyrimvrsavegame.cpp
	skyrimvrsavegame.h
	skyrimvrscriptextender.cpp
	skyrimvrscriptextender.h
	skyrimvrunmanagedmods.cpp
	skyrimvrunmanagedmods.h
)
mo2_configure_plugin(game_skyrimvr NO_SOURCES WARNINGS 4)
mo2_default_source_group()
target_link_libraries(game_skyrimvr PRIVATE game_creation)
mo2_install_plugin(game_skyrimvr)

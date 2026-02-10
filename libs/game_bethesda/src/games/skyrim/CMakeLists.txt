cmake_minimum_required(VERSION 3.16)

add_library(game_skyrim SHARED
	gameskyrim.cpp
	gameskyrim.h
	skyrimbsainvalidation.cpp
	skyrimbsainvalidation.h
	skyrimdataarchives.cpp
	skyrimdataarchives.h
	skyrimgameplugins.cpp
	skyrimgameplugins.h
	skyrimmoddatachecker.h
	skyrimmoddatacontent.h
	skyrimsavegame.cpp
	skyrimsavegame.h
	skyrimscriptextender.cpp
	skyrimscriptextender.h
)
mo2_configure_plugin(game_skyrim NO_SOURCES WARNINGS 4)
mo2_default_source_group()
target_link_libraries(game_skyrim PRIVATE game_gamebryo)
mo2_install_plugin(game_skyrim)

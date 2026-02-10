cmake_minimum_required(VERSION 3.16)

add_library(game_fallout76 SHARED
	fallout76dataarchives.cpp
	fallout76dataarchives.h
	fallout76moddatachecker.h
	fallout76moddatacontent.h
	fallout76savegame.cpp
	fallout76savegame.h
	fallout76savegameinfo.cpp
	fallout76savegameinfo.h
	fallout76scriptextender.cpp
	fallout76scriptextender.h
	fallout76unmanagedmods.cpp
	fallout76unmanagedmods.h
	gamefallout76.cpp
	gamefallout76.h
)
mo2_configure_plugin(game_fallout76 NO_SOURCES WARNINGS 4)
mo2_default_source_group()
target_link_libraries(game_fallout76 PRIVATE game_creation)
mo2_install_plugin(game_fallout76)

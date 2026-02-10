cmake_minimum_required(VERSION 3.16)

add_library(game_fallout4 SHARED
	fallout4bsainvalidation.cpp
	fallout4bsainvalidation.h
	fallout4dataarchives.cpp
	fallout4dataarchives.h
	fallout4moddatachecker.h
	fallout4moddatacontent.h
	fallout4savegame.cpp
	fallout4savegame.h
	fallout4scriptextender.cpp
	fallout4scriptextender.h
	fallout4unmanagedmods.cpp
	fallout4unmanagedmods.h
	gamefallout4.cpp
	gamefallout4.h
)
mo2_configure_plugin(game_fallout4 NO_SOURCES WARNINGS 4)
mo2_default_source_group()
target_link_libraries(game_fallout4 PRIVATE game_creation)
mo2_install_plugin(game_fallout4)

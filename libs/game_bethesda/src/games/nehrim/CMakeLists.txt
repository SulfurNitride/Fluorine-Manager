cmake_minimum_required(VERSION 3.16)

add_library(game_nehrim SHARED
	gamenehrim.cpp
	gamenehrim.h
	nehrimbsainvalidation.cpp
	nehrimbsainvalidation.h
	nehrimdataarchives.cpp
	nehrimdataarchives.h
	nehrimmoddatachecker.cpp
	nehrimmoddatachecker.h
	nehrimmoddatacontent.h
	nehrimsavegame.cpp
	nehrimsavegame.h
	nehrimscriptextender.cpp
	nehrimscriptextender.h
)
mo2_configure_plugin(game_nehrim NO_SOURCES WARNINGS 4)
mo2_default_source_group()
target_link_libraries(game_nehrim PRIVATE game_gamebryo)
mo2_install_plugin(game_nehrim)

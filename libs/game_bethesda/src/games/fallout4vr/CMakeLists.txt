cmake_minimum_required(VERSION 3.16)

add_library(game_fallout4vr SHARED
	fallout4vrdataarchives.cpp
	fallout4vrdataarchives.h
	fallout4vrgameplugins.cpp
	fallout4vrgameplugins.h
	fallout4vrmoddatachecker.h
	fallout4vrmoddatacontent.h
	fallout4vrsavegame.cpp
	fallout4vrsavegame.h
	fallout4vrscriptextender.cpp
	fallout4vrscriptextender.h
	fallout4vrunmanagedmods.cpp
	fallout4vrunmanagedmods.h
	gamefallout4vr.cpp
	gamefallout4vr.h
)
mo2_configure_plugin(game_fallout4vr NO_SOURCES WARNINGS 4)
mo2_default_source_group()
target_link_libraries(game_fallout4vr PRIVATE game_creation)
mo2_install_plugin(game_fallout4vr)

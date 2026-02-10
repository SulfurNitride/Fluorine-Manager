cmake_minimum_required(VERSION 3.16)

add_library(game_enderalse SHARED
	enderalsedataarchives.cpp
	enderalsedataarchives.h
	enderalsegameplugins.cpp
	enderalsegameplugins.h
	enderalselocalsavegames.cpp
	enderalselocalsavegames.h
	enderalsemoddatachecker.h
	enderalsemoddatacontent.h
	enderalsesavegame.cpp
	enderalsesavegame.h
	enderalsescriptextender.cpp
	enderalsescriptextender.h
	enderalseunmanagedmods.cpp
	enderalseunmanagedmods.h
	gameenderalse.cpp
	gameenderalse.h
)
mo2_configure_plugin(game_enderalse NO_SOURCES WARNINGS 4)
mo2_default_source_group()
target_link_libraries(game_enderalse PRIVATE game_creation)
mo2_install_plugin(game_enderalse)

cmake_minimum_required(VERSION 3.16)

add_library(game_fo4london SHARED
	fo4londonbsainvalidation.cpp
	fo4londonbsainvalidation.h
	fo4londondataarchives.cpp
	fo4londondataarchives.h
	fo4londonmoddatachecker.h
	fo4londonmoddatacontent.h
	fo4londonsavegame.cpp
	fo4londonsavegame.h
	fo4londonscriptextender.cpp
	fo4londonscriptextender.h
	fo4londonunmanagedmods.cpp
	fo4londonunmanagedmods.h
	gamefo4london.cpp
	gamefo4london.h)
mo2_configure_plugin(game_fo4london NO_SOURCES WARNINGS 4)
target_link_libraries(game_fo4london PRIVATE game_creation)
mo2_install_plugin(game_fo4london)

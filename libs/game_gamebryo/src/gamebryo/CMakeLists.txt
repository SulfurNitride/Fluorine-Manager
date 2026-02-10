cmake_minimum_required(VERSION 3.16)

add_library(game_gamebryo STATIC)
mo2_configure_library(game_gamebryo
	WARNINGS OFF
	TRANSLATIONS ON
	AUTOMOC ON
	PUBLIC_DEPENDS uibase
	PRIVATE_DEPENDS zlib lz4)
mo2_install_target(game_gamebryo)

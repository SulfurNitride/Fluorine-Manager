cmake_minimum_required(VERSION 3.16)

add_library(game_creation STATIC)
mo2_configure_library(game_creation
	WARNINGS OFF
	TRANSLATIONS ON
	PUBLIC_DEPENDS uibase
	PRIVATE_DEPENDS lz4)
target_link_libraries(game_creation PUBLIC game_gamebryo)
mo2_install_target(game_creation)

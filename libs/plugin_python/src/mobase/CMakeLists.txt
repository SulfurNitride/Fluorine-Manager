cmake_minimum_required(VERSION 3.16)

find_package(Qt6 COMPONENTS Core)
if(NOT TARGET mo2::uibase)
  find_package(mo2-uibase CONFIG REQUIRED)
endif()

pybind11_add_module(mobase MODULE)
mo2_default_source_group()
mo2_configure_target(mobase
	NO_SOURCES
	WARNINGS 4
	EXTERNAL_WARNINGS 4
	AUTOMOC ON
	TRANSLATIONS OFF
)
mo2_target_sources(mobase
	FOLDER src
	PRIVATE
		deprecation.cpp
		deprecation.h
		mobase.cpp
		pybind11_all.h
)
mo2_target_sources(mobase
	FOLDER src/wrappers
	PRIVATE
		./wrappers/basic_classes.cpp
		./wrappers/game_features.cpp
		./wrappers/known_folders.h
		./wrappers/pyfiletree.cpp
		./wrappers/pyfiletree.h
		./wrappers/pyplugins.cpp
		./wrappers/pyplugins.h
		./wrappers/utils.cpp
		./wrappers/widgets.cpp
		./wrappers/wrappers.cpp
		./wrappers/wrappers.h
)
target_link_libraries(mobase PRIVATE pybind11::qt pybind11::utils mo2::uibase Qt6::Core)

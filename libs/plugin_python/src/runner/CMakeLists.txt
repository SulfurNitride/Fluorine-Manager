cmake_minimum_required(VERSION 3.16)

if(NOT TARGET mo2::uibase)
  find_package(mo2-uibase CONFIG REQUIRED)
endif()

add_library(runner SHARED
	error.h
	pythonrunner.cpp
	pythonrunner.h
	pythonutils.h
	pythonutils.cpp
)
mo2_configure_target(runner
	NO_SOURCES
	WARNINGS 4
	EXTERNAL_WARNINGS 4
	AUTOMOC ON
	TRANSLATIONS OFF
)
mo2_default_source_group()
target_link_libraries(runner PUBLIC mo2::uibase PRIVATE pybind11::embed pybind11::qt)
target_include_directories(runner PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(runner PRIVATE RUNNER_BUILD)
if(NOT WIN32 AND Python_SHARED_LIBRARY)
  target_compile_definitions(runner PRIVATE
    MO2_PYTHON_SHARED_LIBRARY="${Python_SHARED_LIBRARY}")
endif()

# proxy will install runner

# force runner to build mobase
add_dependencies(runner mobase)

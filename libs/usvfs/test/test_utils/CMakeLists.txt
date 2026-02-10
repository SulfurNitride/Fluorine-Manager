cmake_minimum_required(VERSION 3.16)

find_package(GTest CONFIG REQUIRED)

add_library(test_utils STATIC
	test_helpers.cpp
	test_helpers.h
)
set_target_properties(test_utils PROPERTIES FOLDER tests)
target_link_libraries(test_utils PUBLIC shared)
target_include_directories(test_utils PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

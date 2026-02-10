cmake_minimum_required(VERSION 3.16)

find_package(GTest CONFIG REQUIRED)

add_library(gtest_utils STATIC
	gtest_utils.cpp
	gtest_utils.h
)
set_target_properties(gtest_utils PROPERTIES FOLDER tests)
target_link_libraries(gtest_utils PUBLIC GTest::gtest)
target_include_directories(gtest_utils PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

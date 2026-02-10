cmake_minimum_required(VERSION 3.16)

find_package(GTest CONFIG REQUIRED)

# binary and binary injected
add_executable(testinject_bin testinject_bin/main.cpp)
usvfs_set_test_properties(testinject_bin FOLDER tinjectlib)

add_library(testinject_dll SHARED testinject_dll/main.cpp testinject_dll/main.h)
usvfs_set_test_properties(testinject_dll FOLDER tinjectlib)

# actual test executable
add_executable(tinjectlib_test main.cpp)
usvfs_set_test_properties(tinjectlib_test FOLDER tinjectlib)
target_link_libraries(tinjectlib_test PRIVATE tinjectlib shared GTest::gtest GTest::gtest_main)
add_dependencies(tinjectlib_test testinject_bin testinject_dll)

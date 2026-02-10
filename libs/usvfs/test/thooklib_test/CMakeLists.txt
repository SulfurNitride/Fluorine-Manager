cmake_minimum_required(VERSION 3.16)

find_package(GTest CONFIG REQUIRED)
find_package(Boost CONFIG REQUIRED COMPONENTS thread)

# not sure why there is a test_hooks.cpp here?
add_executable(thooklib_test main.cpp)
usvfs_set_test_properties(thooklib_test)
target_link_libraries(thooklib_test
    PRIVATE test_utils shared thooklib Boost::thread
    GTest::gtest GTest::gtest_main)

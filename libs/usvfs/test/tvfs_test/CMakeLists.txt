cmake_minimum_required(VERSION 3.16)

find_package(GTest CONFIG REQUIRED)

add_executable(tvfs_test main.cpp)
usvfs_set_test_properties(tvfs_test)
target_link_libraries(tvfs_test PRIVATE test_utils usvfs_helper GTest::gtest GTest::gmock GTest::gtest_main)
usvfs_target_link_usvfs(tvfs_test)

# tvfs_test uses a private USVFS header so we need to include it manually
get_target_property(USVFS_SOURCE_DIR usvfs_dll SOURCE_DIR)
target_include_directories(tvfs_test PRIVATE ${USVFS_SOURCE_DIR})

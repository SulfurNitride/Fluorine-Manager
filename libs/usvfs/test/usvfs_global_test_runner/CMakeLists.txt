cmake_minimum_required(VERSION 3.16)

find_package(GTest CONFIG REQUIRED)
find_package(Boost CONFIG REQUIRED COMPONENTS filesystem)

# other executables
add_executable(usvfs_global_test
    usvfs_global_test/usvfs_global_test.cpp
)
usvfs_set_test_properties(usvfs_global_test FOLDER usvfs_global_test)
target_link_libraries(usvfs_global_test PRIVATE gtest_utils GTest::gtest_main GTest::gmock Boost::filesystem)

# actual test executable
add_executable(usvfs_global_test_runner
    usvfs_global_test_fixture.cpp
    usvfs_global_test_fixture.h
    usvfs_global_test_runner.cpp
)
usvfs_set_test_properties(usvfs_global_test_runner FOLDER usvfs_global_test)
usvfs_target_link_usvfs(usvfs_global_test_runner)
target_link_libraries(usvfs_global_test_runner
    PRIVATE test_utils gtest_utils GTest::gtest_main)
add_dependencies(usvfs_global_test_runner usvfs_global_test)

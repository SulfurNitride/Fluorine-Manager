cmake_minimum_required(VERSION 3.16)

find_package(GTest CONFIG REQUIRED)

# other executables
add_executable(test_file_operations
    test_file_operations/test_file_operations.cpp
    test_file_operations/test_filesystem.cpp
    test_file_operations/test_filesystem.h
    test_file_operations/test_ntapi.cpp
    test_file_operations/test_ntapi.h
    test_file_operations/test_ntdll_declarations.h
    test_file_operations/test_w32api.cpp
    test_file_operations/test_w32api.h
)
usvfs_set_test_properties(test_file_operations FOLDER usvfs_test_runner)
target_link_libraries(test_file_operations PRIVATE test_utils ntdll)

add_executable(usvfs_test
    usvfs_test/usvfs_basic_test.cpp
    usvfs_test/usvfs_basic_test.h
    usvfs_test/usvfs_test_base.cpp
    usvfs_test/usvfs_test_base.h
    usvfs_test/usvfs_test.cpp
)
usvfs_set_test_properties(usvfs_test FOLDER usvfs_test_runner)
usvfs_target_link_usvfs(usvfs_test)
target_link_libraries(usvfs_test PRIVATE test_utils)

# actual test executable
add_executable(usvfs_test_runner usvfs_test_runner.cpp)
usvfs_set_test_properties(usvfs_test_runner FOLDER usvfs_test_runner)
target_link_libraries(usvfs_test_runner
    PRIVATE test_utils GTest::gtest GTest::gtest_main)
add_dependencies(usvfs_test_runner usvfs_test test_file_operations)

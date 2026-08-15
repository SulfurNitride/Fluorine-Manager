# Managed-prefix ownership and deletion-boundary tests.
add_executable(test_fluorineconfigownership EXCLUDE_FROM_ALL
    test_fluorineconfigownership.cpp
    ${CMAKE_SOURCE_DIR}/src/src/fluorineconfig.cpp
    ${CMAKE_SOURCE_DIR}/src/src/fluorinepaths.cpp
)
set_target_properties(test_fluorineconfigownership PROPERTIES
    AUTOMOC OFF
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)
target_include_directories(test_fluorineconfigownership PRIVATE
    ${CMAKE_SOURCE_DIR}/src/src
    ${CMAKE_SOURCE_DIR}/libs/uibase/include
)
target_link_libraries(test_fluorineconfigownership PRIVATE
    Qt6::Core
    mo2::uibase
    GTest::gtest
    GTest::gtest_main
)
add_test(NAME test_fluorineconfigownership COMMAND test_fluorineconfigownership)
add_custom_target(prefix-safety-tests DEPENDS test_fluorineconfigownership)

add_executable(test_wineruntimeconfig EXCLUDE_FROM_ALL
    test_wineruntimeconfig.cpp
    ${CMAKE_SOURCE_DIR}/src/src/wineruntimeconfig.cpp
    ${CMAKE_SOURCE_DIR}/src/src/settingsmigration.cpp
    ${CMAKE_SOURCE_DIR}/src/src/fluorineconfig.cpp
    ${CMAKE_SOURCE_DIR}/src/src/fluorinepaths.cpp
)
set_target_properties(test_wineruntimeconfig PROPERTIES
    AUTOMOC OFF
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)
target_include_directories(test_wineruntimeconfig PRIVATE
    ${CMAKE_SOURCE_DIR}/src/src
    ${CMAKE_SOURCE_DIR}/libs/uibase/include
)
target_link_libraries(test_wineruntimeconfig PRIVATE
    Qt6::Core
    mo2::uibase
    GTest::gtest
)
add_test(NAME test_wineruntimeconfig COMMAND test_wineruntimeconfig)
set_tests_properties(test_wineruntimeconfig PROPERTIES TIMEOUT 15)
add_dependencies(prefix-safety-tests test_wineruntimeconfig)

add_executable(test_protonsettingsedit EXCLUDE_FROM_ALL
    test_protonsettingsedit.cpp
    ${CMAKE_SOURCE_DIR}/src/src/protonsettingsedit.cpp
    ${CMAKE_SOURCE_DIR}/src/src/settingsmigration.cpp
    ${CMAKE_SOURCE_DIR}/src/src/fluorineconfig.cpp
    ${CMAKE_SOURCE_DIR}/src/src/fluorinepaths.cpp
)
set_target_properties(test_protonsettingsedit PROPERTIES
    AUTOMOC OFF
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)
target_include_directories(test_protonsettingsedit PRIVATE
    ${CMAKE_SOURCE_DIR}/src/src
    ${CMAKE_SOURCE_DIR}/libs/uibase/include
)
target_link_libraries(test_protonsettingsedit PRIVATE
    Qt6::Core
    mo2::uibase
    GTest::gtest
    GTest::gtest_main
)
add_test(NAME test_protonsettingsedit COMMAND test_protonsettingsedit)
add_dependencies(prefix-safety-tests test_protonsettingsedit)

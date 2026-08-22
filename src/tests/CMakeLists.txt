cmake_minimum_required(VERSION 3.16)

find_package(Qt6 REQUIRED COMPONENTS Core)
find_package(GTest REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_package(PkgConfig REQUIRED)
pkg_check_modules(TEST_FUSE3 REQUIRED IMPORTED_TARGET fuse3)

include(${CMAKE_CURRENT_SOURCE_DIR}/prefix_safety_tests.cmake)

# ---------------------------------------------------------------------------
# Python regression tests
# ---------------------------------------------------------------------------
add_test(
    NAME test_openmw_cfg
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_openmw_cfg.py
)
add_test(
    NAME test_flatpak_access
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_flatpak_access.py
)
add_test(
    NAME test_epic_utils
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_epic_utils.py
)
add_test(
    NAME test_palworld_game_metadata
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_palworld_game_metadata.py
)
add_test(
    NAME test_openmw_game_plugins
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_openmw_game_plugins.py
)
add_test(
    NAME test_openmw_loot_sort
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_openmw_loot_sort.py
)

# ---------------------------------------------------------------------------
# TES3 metadata and OpenMW Player stub parsing tests
# ---------------------------------------------------------------------------
add_executable(test_esptk_tes3 EXCLUDE_FROM_ALL
    test_esptk_tes3.cpp
)
set_target_properties(test_esptk_tes3 PROPERTIES
    AUTOMOC OFF
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)
target_link_libraries(test_esptk_tes3 PRIVATE
    mo2::esptk
    GTest::gtest
    GTest::gtest_main
)
add_test(NAME test_esptk_tes3 COMMAND test_esptk_tes3)

# ---------------------------------------------------------------------------
# BSA/BA2 extraction path normalization tests
# ---------------------------------------------------------------------------
add_executable(test_bsatk_extraction EXCLUDE_FROM_ALL
    test_bsatk_extraction.cpp
)
set_target_properties(test_bsatk_extraction PROPERTIES
    AUTOMOC OFF
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)
target_compile_definitions(test_bsatk_extraction PRIVATE
    FLUORINE_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
target_link_libraries(test_bsatk_extraction PRIVATE
    mo2::bsatk
    GTest::gtest
    GTest::gtest_main
)
add_test(NAME test_bsatk_extraction COMMAND test_bsatk_extraction)

# ---------------------------------------------------------------------------
# Session-only plugin compatibility policy tests
# ---------------------------------------------------------------------------
add_executable(test_plugincompatibility EXCLUDE_FROM_ALL
    test_plugincompatibility.cpp
    ${CMAKE_SOURCE_DIR}/src/src/plugincompatibility.cpp
)
set_target_properties(test_plugincompatibility PROPERTIES
    AUTOMOC OFF
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)
target_include_directories(test_plugincompatibility PRIVATE
    ${CMAKE_SOURCE_DIR}/src/src
)
target_link_libraries(test_plugincompatibility PRIVATE
    Qt6::Core
    GTest::gtest
    GTest::gtest_main
)
add_test(NAME test_plugincompatibility COMMAND test_plugincompatibility)

# ---------------------------------------------------------------------------
# Per-instance FUSE/USVFS selection and Wine launch-request serialization
# ---------------------------------------------------------------------------
add_executable(test_vfs_backend EXCLUDE_FROM_ALL
    test_vfs_backend.cpp
    ${CMAKE_SOURCE_DIR}/src/src/vfsbackend.cpp
    ${CMAKE_SOURCE_DIR}/src/src/usvfsrequest.cpp
)
set_target_properties(test_vfs_backend PROPERTIES
    AUTOMOC OFF
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)
target_include_directories(test_vfs_backend PRIVATE
    ${CMAKE_SOURCE_DIR}/src/src
)
target_link_libraries(test_vfs_backend PRIVATE
    Qt6::Core
    mo2::uibase
    GTest::gtest
    GTest::gtest_main
)
add_test(NAME test_vfs_backend COMMAND test_vfs_backend)

# ---------------------------------------------------------------------------
# meta.ini case-normalization helper tests
# ---------------------------------------------------------------------------
add_executable(test_metainiutils EXCLUDE_FROM_ALL
    test_metainiutils.cpp
    ${CMAKE_SOURCE_DIR}/src/src/metainiutils.cpp
)
set_target_properties(test_metainiutils PROPERTIES
    AUTOMOC OFF
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)
target_include_directories(test_metainiutils PRIVATE
    ${CMAKE_SOURCE_DIR}/src/src
)
target_link_libraries(test_metainiutils PRIVATE
    Qt6::Core
    GTest::gtest
    GTest::gtest_main
)
add_test(NAME test_metainiutils COMMAND test_metainiutils)

# ---------------------------------------------------------------------------
# Heroic/Legendary JSON schema normalization tests
# ---------------------------------------------------------------------------
add_executable(test_heroicjson EXCLUDE_FROM_ALL
    test_heroicjson.cpp
    ${CMAKE_SOURCE_DIR}/src/src/heroicjson.cpp
)
set_target_properties(test_heroicjson PROPERTIES
    AUTOMOC OFF
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)
target_include_directories(test_heroicjson PRIVATE
    ${CMAKE_SOURCE_DIR}/src/src
)
target_link_libraries(test_heroicjson PRIVATE
    Qt6::Core
    GTest::gtest
    GTest::gtest_main
)
add_test(NAME test_heroicjson COMMAND test_heroicjson)

# ---------------------------------------------------------------------------
# Proton discovery tests (including Heroic without Steam)
# ---------------------------------------------------------------------------
add_executable(test_protondetection EXCLUDE_FROM_ALL
    test_protondetection.cpp
)
set_target_properties(test_protondetection PROPERTIES
    AUTOMOC OFF
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)
target_include_directories(test_protondetection PRIVATE
    ${CMAKE_SOURCE_DIR}/src/src
)
target_link_libraries(test_protondetection PRIVATE
    Qt6::Core
    mo2::uibase
    GTest::gtest
    GTest::gtest_main
)
add_test(NAME test_protondetection COMMAND test_protondetection)

# ---------------------------------------------------------------------------
# External directory mapping (SKSE Log Redirector style) tests
#
# Exercises the deploy/cleanup logic for `isDirectory && createTarget`
# mappings — directory-symlink path plus the per-file fallback when the
# destination already has real content. The behavior is mirrored from
# FuseConnector::deployExternalMappings so the test can run without
# bringing in libfuse or the rest of the organizer dependency tree.
# ---------------------------------------------------------------------------
add_executable(test_external_dir_mapping EXCLUDE_FROM_ALL
    test_external_dir_mapping.cpp
)
set_target_properties(test_external_dir_mapping PROPERTIES
    AUTOMOC OFF
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)
target_link_libraries(test_external_dir_mapping PRIVATE
    GTest::gtest
    GTest::gtest_main
)
add_test(NAME test_external_dir_mapping COMMAND test_external_dir_mapping)

# ---------------------------------------------------------------------------
# Recursive shell move/overwrite behavior used by mod drag-and-drop.
# ---------------------------------------------------------------------------
add_executable(test_shell_operations EXCLUDE_FROM_ALL
    test_shell_operations.cpp
)
set_target_properties(test_shell_operations PROPERTIES
    AUTOMOC OFF
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)
target_link_libraries(test_shell_operations PRIVATE
    Qt6::Core
    mo2::uibase
    GTest::gtest
    GTest::gtest_main
)
add_test(NAME test_shell_operations COMMAND test_shell_operations)

# ---------------------------------------------------------------------------
# VFS file-operation regression tests
# ---------------------------------------------------------------------------
add_executable(test_vfs_file_ops EXCLUDE_FROM_ALL
    test_vfs_file_ops.cpp
    ${CMAKE_SOURCE_DIR}/src/src/vfs/inodetable.cpp
    ${CMAKE_SOURCE_DIR}/src/src/vfs/overwritemanager.cpp
    ${CMAKE_SOURCE_DIR}/src/src/vfs/vfstree.cpp
)
set_target_properties(test_vfs_file_ops PROPERTIES
    AUTOMOC OFF
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)
target_include_directories(test_vfs_file_ops PRIVATE
    ${CMAKE_SOURCE_DIR}/src/src
)
target_link_libraries(test_vfs_file_ops PRIVATE
    GTest::gtest
    GTest::gtest_main
)
add_test(NAME test_vfs_file_ops COMMAND test_vfs_file_ops)

# ---------------------------------------------------------------------------
# FUSE shutdown descriptor ownership/cleanup tests
# ---------------------------------------------------------------------------
add_executable(test_vfs_descriptor_cleanup EXCLUDE_FROM_ALL
    test_vfs_descriptor_cleanup.cpp
    ${CMAKE_SOURCE_DIR}/src/src/vfs/openfilecleanup.cpp
)
set_target_properties(test_vfs_descriptor_cleanup PROPERTIES
    AUTOMOC OFF
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)
target_include_directories(test_vfs_descriptor_cleanup PRIVATE
    ${CMAKE_SOURCE_DIR}/src/src
)
target_compile_definitions(test_vfs_descriptor_cleanup PRIVATE
    FUSE_USE_VERSION=312
)
target_link_libraries(test_vfs_descriptor_cleanup PRIVATE
    PkgConfig::TEST_FUSE3
    GTest::gtest
    GTest::gtest_main
)
add_test(NAME test_vfs_descriptor_cleanup COMMAND test_vfs_descriptor_cleanup)

# ---------------------------------------------------------------------------
# Immutable runtime lookup/metadata index tests
# ---------------------------------------------------------------------------
add_executable(test_vfs_runtime_index EXCLUDE_FROM_ALL
    test_vfs_runtime_index.cpp
    ${CMAKE_SOURCE_DIR}/src/src/vfs/runtimeindex.cpp
    ${CMAKE_SOURCE_DIR}/src/src/vfs/inodetable.cpp
    ${CMAKE_SOURCE_DIR}/src/src/vfs/vfstree.cpp
)
set_target_properties(test_vfs_runtime_index PROPERTIES
    AUTOMOC OFF
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)
target_include_directories(test_vfs_runtime_index PRIVATE
    ${CMAKE_SOURCE_DIR}/src/src
)
target_link_libraries(test_vfs_runtime_index PRIVATE
    GTest::gtest
    GTest::gtest_main
)
add_test(NAME test_vfs_runtime_index COMMAND test_vfs_runtime_index)

# ---------------------------------------------------------------------------
# Bethesda single-stream zlib save parsing and corrupt-data safety
# ---------------------------------------------------------------------------
add_executable(test_gamebryo_save_compression EXCLUDE_FROM_ALL
    test_gamebryo_save_compression.cpp
)
set_target_properties(test_gamebryo_save_compression PROPERTIES
    AUTOMOC OFF
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)
target_compile_options(test_gamebryo_save_compression PRIVATE
    "-idirafter" "${CMAKE_SOURCE_DIR}/libs/uibase/include/uibase"
)
target_link_libraries(test_gamebryo_save_compression PRIVATE
    game_gamebryo
    Qt6::Core
    GTest::gtest
    GTest::gtest_main
)
add_test(NAME test_gamebryo_save_compression COMMAND test_gamebryo_save_compression)

# ---------------------------------------------------------------------------
# Game-local save migration tests
# ---------------------------------------------------------------------------
add_executable(test_game_save_migration EXCLUDE_FROM_ALL
    test_game_save_migration.cpp
)
set_target_properties(test_game_save_migration PROPERTIES
    AUTOMOC OFF
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)
target_include_directories(test_game_save_migration PRIVATE
    ${CMAKE_SOURCE_DIR}/src/src
)
target_link_libraries(test_game_save_migration PRIVATE
    GTest::gtest
    GTest::gtest_main
)
add_test(NAME test_game_save_migration COMMAND test_game_save_migration)

# ---------------------------------------------------------------------------
# Crash-safe consumer-facing VFS publication/validation contract tests
# ---------------------------------------------------------------------------
add_executable(test_vfs_index EXCLUDE_FROM_ALL
    test_vfs_index.cpp
    ${CMAKE_SOURCE_DIR}/src/src/vfs/vfsindex.cpp
    ${CMAKE_SOURCE_DIR}/src/src/vfs/vfstree.cpp
)
set_target_properties(test_vfs_index PROPERTIES
    AUTOMOC OFF
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)
target_include_directories(test_vfs_index PRIVATE
    ${CMAKE_SOURCE_DIR}/src/src
)
target_link_libraries(test_vfs_index PRIVATE
    Qt6::Core
    SQLite::SQLite3
    BLAKE3::blake3
    utf8proc
    GTest::gtest
    GTest::gtest_main
)
add_test(NAME test_vfs_index COMMAND test_vfs_index)

# ---------------------------------------------------------------------------
# Persistent VFS catalog / BLAKE3 verification tests
# ---------------------------------------------------------------------------
add_executable(test_vfs_catalog EXCLUDE_FROM_ALL
    test_vfs_catalog.cpp
    ${CMAKE_SOURCE_DIR}/src/src/usvfssnapshot.cpp
    ${CMAKE_SOURCE_DIR}/src/src/vfs/vfscatalog.cpp
    ${CMAKE_SOURCE_DIR}/src/src/vfs/vfstree.cpp
    ${CMAKE_SOURCE_DIR}/src/src/vfs/permissionrepair.cpp
    ${CMAKE_SOURCE_DIR}/src/src/fluorinepaths.cpp
    ${CMAKE_SOURCE_DIR}/src/src/fluorineconfig.cpp
)
set_target_properties(test_vfs_catalog PROPERTIES
    AUTOMOC OFF
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)
target_include_directories(test_vfs_catalog PRIVATE
    ${CMAKE_SOURCE_DIR}/src/src
    ${CMAKE_SOURCE_DIR}/libs/uibase/include
)
target_link_libraries(test_vfs_catalog PRIVATE
    Qt6::Core
    SQLite::SQLite3
    BLAKE3::blake3
    mo2::uibase
    GTest::gtest
    GTest::gtest_main
)
target_compile_definitions(test_vfs_catalog PRIVATE
    FLUORINE_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
if(TARGET mo2::bsa_ffi)
    target_link_libraries(test_vfs_catalog PRIVATE mo2::bsa_ffi)
    target_compile_definitions(test_vfs_catalog PRIVATE FLUORINE_HAS_BSA_FFI=1)
endif()
add_test(NAME test_vfs_catalog COMMAND test_vfs_catalog)

# ---------------------------------------------------------------------------
# Crash-safe VFS staging promotion tests
# ---------------------------------------------------------------------------
add_executable(test_staging_promotion EXCLUDE_FROM_ALL
    test_stagingpromotion.cpp
    ${CMAKE_SOURCE_DIR}/src/src/vfs/stagingpromotion.cpp
)
set_target_properties(test_staging_promotion PROPERTIES
    AUTOMOC OFF
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)
target_include_directories(test_staging_promotion PRIVATE
    ${CMAKE_SOURCE_DIR}/src/src
)
target_link_libraries(test_staging_promotion PRIVATE
    Qt6::Core
    BLAKE3::blake3
    GTest::gtest
    GTest::gtest_main
)
add_test(NAME test_staging_promotion COMMAND test_staging_promotion)

# ---------------------------------------------------------------------------
# Embedded-Python package replacement across in-process instance switches
# ---------------------------------------------------------------------------
add_executable(test_python_module_switch EXCLUDE_FROM_ALL
    ${CMAKE_SOURCE_DIR}/libs/plugin_python/tests/runner/test_module_switch.cpp
)
set_target_properties(test_python_module_switch PROPERTIES
    AUTOMOC OFF
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)
target_include_directories(test_python_module_switch PRIVATE
    ${CMAKE_SOURCE_DIR}/libs/plugin_python/src/runner
)
target_link_libraries(test_python_module_switch PRIVATE
    runner
    GTest::gtest
    GTest::gtest_main
)
add_test(NAME test_python_module_switch COMMAND test_python_module_switch)
set_tests_properties(test_python_module_switch PROPERTIES
    ENVIRONMENT
        "PLUGIN_DIR=${CMAKE_SOURCE_DIR}/libs/plugin_python/tests/runner/plugins;PYTHONPATH=$<TARGET_FILE_DIR:mobase>"
)

# Register a "fluorine-tests" umbrella target that builds every test exe.
add_custom_target(fluorine-tests
    DEPENDS test_esptk_tes3 test_bsatk_extraction
            test_plugincompatibility test_metainiutils
            test_vfs_backend
            test_fluorineconfigownership
            test_heroicjson test_protondetection
            test_external_dir_mapping test_vfs_file_ops
            test_shell_operations
            test_vfs_descriptor_cleanup test_vfs_runtime_index
            test_gamebryo_save_compression
            test_game_save_migration
            test_vfs_catalog test_vfs_index
            test_staging_promotion test_python_module_switch)

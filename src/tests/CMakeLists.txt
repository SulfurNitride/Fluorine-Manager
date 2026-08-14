cmake_minimum_required(VERSION 3.16)

find_package(Qt6 REQUIRED COMPONENTS Core Network Widgets)
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
add_test(
    NAME test_openmw_process_lifetime
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test_openmw_process_lifetime.py
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
# Transactional proxied-plugin registration policy tests
# ---------------------------------------------------------------------------
add_executable(test_pluginregistrationpolicy EXCLUDE_FROM_ALL
    test_pluginregistrationpolicy.cpp
)
set_target_properties(test_pluginregistrationpolicy PROPERTIES
    AUTOMOC OFF
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)
target_include_directories(test_pluginregistrationpolicy PRIVATE
    ${CMAKE_SOURCE_DIR}/src/src
)
target_link_libraries(test_pluginregistrationpolicy PRIVATE
    Qt6::Core
    GTest::gtest
    GTest::gtest_main
)
add_test(NAME test_pluginregistrationpolicy COMMAND test_pluginregistrationpolicy)

# ---------------------------------------------------------------------------
# Product-independent settings migration schema tests
# ---------------------------------------------------------------------------
add_executable(test_settingsmigration EXCLUDE_FROM_ALL
    test_settingsmigration.cpp
    ${CMAKE_SOURCE_DIR}/src/src/categoryfileparser.cpp
    ${CMAKE_SOURCE_DIR}/src/src/categorypersistence.cpp
    ${CMAKE_SOURCE_DIR}/src/src/settingsmigration.cpp
)
set_target_properties(test_settingsmigration PROPERTIES
    AUTOMOC OFF
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)
target_include_directories(test_settingsmigration PRIVATE
    ${CMAKE_SOURCE_DIR}/src/src
)
target_link_libraries(test_settingsmigration PRIVATE
    Qt6::Core
    GTest::gtest
    GTest::gtest_main
)
add_test(NAME test_settingsmigration COMMAND test_settingsmigration)

# ---------------------------------------------------------------------------
# Launcher/companion process lifetime tracking tests
# ---------------------------------------------------------------------------
add_executable(test_processlifetime EXCLUDE_FROM_ALL
    test_processlifetime.cpp
    ${CMAKE_SOURCE_DIR}/src/src/processlifetime.cpp
)
set_target_properties(test_processlifetime PROPERTIES
    AUTOMOC OFF
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)
target_include_directories(test_processlifetime PRIVATE
    ${CMAKE_SOURCE_DIR}/src/src
)
target_link_libraries(test_processlifetime PRIVATE
    Qt6::Core
    mo2::uibase
    GTest::gtest
    GTest::gtest_main
)
add_test(NAME test_processlifetime COMMAND test_processlifetime)
set_tests_properties(test_processlifetime PROPERTIES TIMEOUT 60)

# ---------------------------------------------------------------------------
# Launch ownership and refresh-generation coordination tests
# ---------------------------------------------------------------------------
add_executable(test_launchcoordination EXCLUDE_FROM_ALL
    test_launchcoordination.cpp
    ${CMAKE_SOURCE_DIR}/src/src/apiuseraccount.cpp
)
set_target_properties(test_launchcoordination PROPERTIES
    AUTOMOC OFF
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)
target_include_directories(test_launchcoordination PRIVATE
    ${CMAKE_SOURCE_DIR}/src/src
)
target_link_libraries(test_launchcoordination PRIVATE
    Qt6::Core
    GTest::gtest
    GTest::gtest_main
)
add_test(NAME test_launchcoordination COMMAND test_launchcoordination)

# ---------------------------------------------------------------------------
# Unix process termination and inherited runtime publication lease tests
# ---------------------------------------------------------------------------
add_executable(test_unixshutdown EXCLUDE_FROM_ALL
    test_unixshutdown.cpp
    ${CMAKE_SOURCE_DIR}/src/src/runtimelock.cpp
    ${CMAKE_SOURCE_DIR}/src/src/unixtermination.cpp
)
set_target_properties(test_unixshutdown PROPERTIES
    AUTOMOC OFF
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)
target_include_directories(test_unixshutdown PRIVATE
    ${CMAKE_SOURCE_DIR}/src/src
)
target_link_libraries(test_unixshutdown PRIVATE
    Qt6::Core
    GTest::gtest
    GTest::gtest_main
)
add_test(NAME test_unixshutdown COMMAND test_unixshutdown)
set_tests_properties(test_unixshutdown PROPERTIES TIMEOUT 15)

# ---------------------------------------------------------------------------
# Game-feature facade provenance and owner-scoped registration tests
# ---------------------------------------------------------------------------
add_executable(test_gamefeatureownership EXCLUDE_FROM_ALL
    test_gamefeatureownership.cpp
)
set_target_properties(test_gamefeatureownership PROPERTIES
    AUTOMOC OFF
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)
target_include_directories(test_gamefeatureownership PRIVATE
    ${CMAKE_SOURCE_DIR}/src/src
    ${CMAKE_SOURCE_DIR}/libs/uibase/include/uibase/game_features
)
target_link_libraries(test_gamefeatureownership PRIVATE
    Qt6::Core
    mo2::uibase
    GTest::gtest
    GTest::gtest_main
)
add_test(NAME test_gamefeatureownership COMMAND test_gamefeatureownership)

# ---------------------------------------------------------------------------
# Steam Linux Runtime fail-stop admission/cancellation tests
# ---------------------------------------------------------------------------
add_executable(test_slroperationcontext EXCLUDE_FROM_ALL
    test_slroperationcontext.cpp
)
set_target_properties(test_slroperationcontext PROPERTIES
    AUTOMOC OFF
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)
target_include_directories(test_slroperationcontext PRIVATE
    ${CMAKE_SOURCE_DIR}/src/src
)
target_link_libraries(test_slroperationcontext PRIVATE
    Qt6::Core
    Qt6::Network
    mo2::uibase
    GTest::gtest
    GTest::gtest_main
)
add_test(NAME test_slroperationcontext COMMAND test_slroperationcontext)

# ---------------------------------------------------------------------------
# Plugin loaded-generation reload refusal and callback gate tests
# ---------------------------------------------------------------------------
add_executable(test_pluginreloadpolicy EXCLUDE_FROM_ALL
    test_pluginreloadpolicy.cpp
)
set_target_properties(test_pluginreloadpolicy PROPERTIES
    AUTOMOC OFF
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)
target_include_directories(test_pluginreloadpolicy PRIVATE
    ${CMAKE_SOURCE_DIR}/src/src
)
target_link_libraries(test_pluginreloadpolicy PRIVATE
    Qt6::Core
    GTest::gtest
)
add_test(NAME test_pluginreloadpolicy COMMAND test_pluginreloadpolicy)

# ---------------------------------------------------------------------------
# Launch teardown exception safety and synchronous refresh wake-up tests
# ---------------------------------------------------------------------------
add_executable(test_launchlifecycle EXCLUDE_FROM_ALL
    test_launchlifecycle.cpp
)
set_target_properties(test_launchlifecycle PROPERTIES
    AUTOMOC OFF
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)
target_include_directories(test_launchlifecycle PRIVATE
    ${CMAKE_SOURCE_DIR}/src/src
)
target_link_libraries(test_launchlifecycle PRIVATE
    Qt6::Core
    GTest::gtest
)
add_test(NAME test_launchlifecycle COMMAND test_launchlifecycle)

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
# Disconnected Fluorine FUSE mount classification tests
# ---------------------------------------------------------------------------
add_executable(test_stalemountcleanup EXCLUDE_FROM_ALL
    test_stalemountcleanup.cpp
    ${CMAKE_SOURCE_DIR}/src/src/vfs/stalemountcleanup.cpp
)
set_target_properties(test_stalemountcleanup PROPERTIES
    AUTOMOC OFF
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)
target_include_directories(test_stalemountcleanup PRIVATE
    ${CMAKE_SOURCE_DIR}/src/src
)
target_link_libraries(test_stalemountcleanup PRIVATE
    Qt6::Core
    GTest::gtest
    GTest::gtest_main
)
add_test(NAME test_stalemountcleanup COMMAND test_stalemountcleanup)

# ---------------------------------------------------------------------------
# Per-user single-instance IPC security and framing tests
# ---------------------------------------------------------------------------
add_executable(test_multiprocess EXCLUDE_FROM_ALL
    test_multiprocess.cpp
    ${CMAKE_SOURCE_DIR}/src/src/multiprocess.cpp
    ${CMAKE_SOURCE_DIR}/src/src/multiprocessprotocol.cpp
)
set_target_properties(test_multiprocess PROPERTIES
    AUTOMOC ON
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)
target_include_directories(test_multiprocess PRIVATE
    ${CMAKE_SOURCE_DIR}/src/src
)
target_link_libraries(test_multiprocess PRIVATE
    Qt6::Core
    Qt6::Network
    mo2::uibase
    GTest::gtest
)
add_test(NAME test_multiprocess COMMAND test_multiprocess)
set_tests_properties(test_multiprocess PROPERTIES TIMEOUT 30)

# ---------------------------------------------------------------------------
# External IPC readiness queue tests
# ---------------------------------------------------------------------------
add_executable(test_externalmessagequeue EXCLUDE_FROM_ALL
    test_externalmessagequeue.cpp
    ${CMAKE_SOURCE_DIR}/src/src/externalmessagequeue.cpp
)
target_include_directories(test_externalmessagequeue PRIVATE
    ${CMAKE_SOURCE_DIR}/src/src
)
target_link_libraries(test_externalmessagequeue PRIVATE
    Qt6::Core
    GTest::gtest_main
)
add_test(NAME test_externalmessagequeue COMMAND test_externalmessagequeue)

# ---------------------------------------------------------------------------
# Portable instance launcher-script tests
# ---------------------------------------------------------------------------
add_executable(test_portablelauncherscript EXCLUDE_FROM_ALL
    test_portablelauncherscript.cpp
    ${CMAKE_SOURCE_DIR}/src/src/portablelauncherscript.cpp
)
set_target_properties(test_portablelauncherscript PROPERTIES
    AUTOMOC OFF
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)
target_include_directories(test_portablelauncherscript PRIVATE
    ${CMAKE_SOURCE_DIR}/src/src
)
target_link_libraries(test_portablelauncherscript PRIVATE
    Qt6::Core
    GTest::gtest
    GTest::gtest_main
)
add_test(NAME test_portablelauncherscript COMMAND test_portablelauncherscript)

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

# ---------------------------------------------------------------------------
# Installed/portable stylesheet lookup and containment tests
# ---------------------------------------------------------------------------
add_executable(test_stylesheetpath EXCLUDE_FROM_ALL
    test_stylesheetpath.cpp
    ${CMAKE_SOURCE_DIR}/src/src/stylesheetpath.cpp
    ${CMAKE_SOURCE_DIR}/src/src/shared/appconfig.cpp
)
set_target_properties(test_stylesheetpath PROPERTIES
    AUTOMOC OFF
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)
target_include_directories(test_stylesheetpath PRIVATE
    ${CMAKE_SOURCE_DIR}/src/src
)
target_link_libraries(test_stylesheetpath PRIVATE
    Qt6::Core
    GTest::gtest
    GTest::gtest_main
)
add_test(NAME test_stylesheetpath COMMAND test_stylesheetpath)

# ---------------------------------------------------------------------------
# Atomic application appearance transitions
# ---------------------------------------------------------------------------
add_executable(test_applicationappearance EXCLUDE_FROM_ALL
    test_applicationappearance.cpp
    ${CMAKE_SOURCE_DIR}/src/src/applicationappearance.cpp
    ${CMAKE_SOURCE_DIR}/src/src/stylesheetpath.cpp
    ${CMAKE_SOURCE_DIR}/src/src/shared/appconfig.cpp
)
set_target_properties(test_applicationappearance PROPERTIES
    AUTOMOC OFF
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)
target_include_directories(test_applicationappearance PRIVATE
    ${CMAKE_SOURCE_DIR}/src/src
)
target_link_libraries(test_applicationappearance PRIVATE
    Qt6::Widgets
    mo2::uibase
    GTest::gtest
)
add_test(NAME test_applicationappearance COMMAND test_applicationappearance)

# ---------------------------------------------------------------------------
# Per-plugin legacy resource path compatibility tests
# ---------------------------------------------------------------------------
add_executable(test_plugindatapath EXCLUDE_FROM_ALL
    test_plugindatapath.cpp
    ${CMAKE_SOURCE_DIR}/src/src/plugindatapath.cpp
)
set_target_properties(test_plugindatapath PROPERTIES
    AUTOMOC OFF
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)
target_include_directories(test_plugindatapath PRIVATE
    ${CMAKE_SOURCE_DIR}/src/src
)
target_link_libraries(test_plugindatapath PRIVATE
    Qt6::Core
    GTest::gtest
    GTest::gtest_main
)
add_test(NAME test_plugindatapath COMMAND test_plugindatapath)

# ---------------------------------------------------------------------------
# Stable physical identity for instance directory aliases
# ---------------------------------------------------------------------------
add_executable(test_instancepathidentity EXCLUDE_FROM_ALL
    test_instancepathidentity.cpp
    ${CMAKE_SOURCE_DIR}/src/src/instancepathidentity.cpp
)
set_target_properties(test_instancepathidentity PROPERTIES
    AUTOMOC OFF
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)
target_include_directories(test_instancepathidentity PRIVATE
    ${CMAKE_SOURCE_DIR}/src/src
)
target_link_libraries(test_instancepathidentity PRIVATE
    Qt6::Core
    GTest::gtest
    GTest::gtest_main
)
add_test(NAME test_instancepathidentity COMMAND test_instancepathidentity)

# ---------------------------------------------------------------------------
# Lossless Unix argv decoding and structured command forwarding
# ---------------------------------------------------------------------------
add_executable(test_commandlinearguments EXCLUDE_FROM_ALL
    test_commandlinearguments.cpp
    ${CMAKE_SOURCE_DIR}/src/src/commandlinearguments.cpp
)
set_target_properties(test_commandlinearguments PROPERTIES
    AUTOMOC OFF
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)
target_include_directories(test_commandlinearguments PRIVATE
    ${CMAKE_SOURCE_DIR}/src/src
)
target_link_libraries(test_commandlinearguments PRIVATE
    Qt6::Core
    GTest::gtest
    GTest::gtest_main
)
add_test(NAME test_commandlinearguments COMMAND test_commandlinearguments)
set_tests_properties(test_commandlinearguments PROPERTIES TIMEOUT 10)

# ---------------------------------------------------------------------------
# NXM and mod.pub direct-download request classification
# ---------------------------------------------------------------------------
add_executable(test_nxmrequest EXCLUDE_FROM_ALL
    test_nxmrequest.cpp
    ${CMAKE_SOURCE_DIR}/src/src/nxmrequest.cpp
)
set_target_properties(test_nxmrequest PROPERTIES
    AUTOMOC OFF
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)
target_include_directories(test_nxmrequest PRIVATE
    ${CMAKE_SOURCE_DIR}/src/src
)
target_link_libraries(test_nxmrequest PRIVATE
    Qt6::Core
    GTest::gtest
    GTest::gtest_main
)
add_test(NAME test_nxmrequest COMMAND test_nxmrequest)
set_tests_properties(test_nxmrequest PROPERTIES TIMEOUT 10)

# ---------------------------------------------------------------------------
# Serialized, recoverable bundle publication tests
# ---------------------------------------------------------------------------
add_test(NAME test_fluorine_publisher
    COMMAND ${Python3_EXECUTABLE} -m unittest -v
            ${CMAKE_CURRENT_SOURCE_DIR}/test_fluorine_publisher.py
)
set_tests_properties(test_fluorine_publisher PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 30
)

# Register a "fluorine-tests" umbrella target that builds every test exe.
add_custom_target(fluorine-tests
    DEPENDS test_esptk_tes3 test_bsatk_extraction
            test_plugincompatibility test_pluginregistrationpolicy
            test_settingsmigration test_processlifetime
            test_launchcoordination test_unixshutdown test_gamefeatureownership
            test_slroperationcontext test_pluginreloadpolicy test_launchlifecycle
            test_metainiutils
            test_vfs_backend
            test_fluorineconfigownership test_protonsettingsedit
            test_heroicjson test_protondetection
            test_external_dir_mapping test_stalemountcleanup
            test_multiprocess test_externalmessagequeue
            test_portablelauncherscript test_vfs_file_ops
            test_vfs_descriptor_cleanup test_vfs_runtime_index
            test_game_save_migration
            test_vfs_catalog test_vfs_index
            test_staging_promotion test_python_module_switch
            test_stylesheetpath test_applicationappearance test_plugindatapath
            test_instancepathidentity test_commandlinearguments
            test_nxmrequest)

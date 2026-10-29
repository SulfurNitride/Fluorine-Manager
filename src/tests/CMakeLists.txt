cmake_minimum_required(VERSION 3.16)

find_package(Qt6 REQUIRED COMPONENTS Core Network)
find_package(GTest REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(ZLIB REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_test(NAME test_bundle_launcher
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_bundle_launcher.py)
add_test(NAME test_build_dependency_repair
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_build_dependency_repair.py)

add_executable(test_xdgpaths EXCLUDE_FROM_ALL
    test_xdgpaths.cpp
    ${CMAKE_SOURCE_DIR}/src/src/fluorinepaths.cpp
    ${CMAKE_SOURCE_DIR}/src/src/fluorineconfig.cpp
    ${CMAKE_SOURCE_DIR}/src/src/nxmhandler_linux.cpp
    ${CMAKE_SOURCE_DIR}/src/src/nxmhandler_linux.h)
set_target_properties(test_xdgpaths PROPERTIES AUTOMOC ON CXX_STANDARD 23)
target_include_directories(test_xdgpaths PRIVATE ${CMAKE_SOURCE_DIR}/src/src)
target_compile_options(test_xdgpaths PRIVATE
    "-idirafter" "${CMAKE_SOURCE_DIR}/libs/uibase/include/uibase")
target_link_libraries(test_xdgpaths PRIVATE
    Qt6::Core Qt6::Network Qt6::DBus mo2::uibase GTest::gtest)
add_test(NAME test_xdgpaths COMMAND test_xdgpaths)
find_package(PkgConfig REQUIRED)
pkg_check_modules(TEST_FUSE3 REQUIRED IMPORTED_TARGET fuse3)

include(${CMAKE_CURRENT_SOURCE_DIR}/prefix_safety_tests.cmake)

add_executable(test_gamepath EXCLUDE_FROM_ALL test_gamepath.cpp)
set_target_properties(test_gamepath PROPERTIES AUTOMOC OFF CXX_STANDARD 23)
target_include_directories(test_gamepath PRIVATE ${CMAKE_SOURCE_DIR}/src/src)
target_link_libraries(test_gamepath PRIVATE
    Qt6::Core mo2::uibase GTest::gtest GTest::gtest_main)
add_test(NAME test_gamepath COMMAND test_gamepath)

# ---------------------------------------------------------------------------
# Python regression tests
# ---------------------------------------------------------------------------
add_test(
    NAME test_wine_paths
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_wine_paths.py
)
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
    NAME test_wabbajack_compatibility
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test_wabbajack_compatibility.py
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

add_executable(test_falloutnv_plugin_types EXCLUDE_FROM_ALL
    test_falloutnv_plugin_types.cpp)
set_target_properties(test_falloutnv_plugin_types PROPERTIES AUTOMOC OFF CXX_STANDARD 23)
target_compile_options(test_falloutnv_plugin_types PRIVATE
    "-idirafter" "${CMAKE_SOURCE_DIR}/libs/uibase/include/uibase")
target_link_libraries(test_falloutnv_plugin_types PRIVATE
    mo2::esptk mo2::uibase Qt6::Widgets bsplugins ZLIB::ZLIB GTest::gtest_main)
add_test(NAME test_falloutnv_plugin_types COMMAND test_falloutnv_plugin_types)

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
# Deterministic PE Large Address Aware patching used by older-game adapters
# ---------------------------------------------------------------------------
add_executable(test_pefileutils EXCLUDE_FROM_ALL
    test_pefileutils.cpp
    ${CMAKE_SOURCE_DIR}/src/src/pefileutils.cpp
)
set_target_properties(test_pefileutils PROPERTIES
    AUTOMOC OFF
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)
target_include_directories(test_pefileutils PRIVATE
    ${CMAKE_SOURCE_DIR}/src/src
)
target_link_libraries(test_pefileutils PRIVATE
    Qt6::Core
    GTest::gtest
    GTest::gtest_main
)
add_test(NAME test_pefileutils COMMAND test_pefileutils)

# ---------------------------------------------------------------------------
# Curated guide recipe validation and resumable state journal
# ---------------------------------------------------------------------------
add_executable(test_curatedguide EXCLUDE_FROM_ALL
    test_curatedguide.cpp
    curatedguide_test_resources.qrc
    ${CMAKE_SOURCE_DIR}/src/src/curatedfomod.cpp
    ${CMAKE_SOURCE_DIR}/src/src/curatedgamemanifest.cpp
    ${CMAKE_SOURCE_DIR}/src/src/curatedinstancebootstrap.cpp
    ${CMAKE_SOURCE_DIR}/src/src/curatedmodlayout.cpp
    ${CMAKE_SOURCE_DIR}/src/src/curatedguiderecipe.cpp
    ${CMAKE_SOURCE_DIR}/src/src/curatedguideinstallstate.cpp
    ${CMAKE_SOURCE_DIR}/src/src/vdfparser.cpp
)
set_target_properties(test_curatedguide PROPERTIES
    AUTOMOC OFF
    AUTORCC ON
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)
target_include_directories(test_curatedguide PRIVATE
    ${CMAKE_SOURCE_DIR}/src/src
)
target_compile_definitions(test_curatedguide PRIVATE
    FLUORINE_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
target_link_libraries(test_curatedguide PRIVATE
    Qt6::Core
    Qt6::Network
    BLAKE3::blake3
    ZLIB::ZLIB
    GTest::gtest
    GTest::gtest_main
)
add_test(NAME test_curatedguide COMMAND test_curatedguide)

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
# FOMOD dependency-transition and rerun recommendation tests
# ---------------------------------------------------------------------------
add_executable(test_fomoddependency EXCLUDE_FROM_ALL
    test_fomoddependency.cpp
    ${CMAKE_SOURCE_DIR}/libs/installer_fomod/src/fomoddependency.cpp
)
set_target_properties(test_fomoddependency PROPERTIES
    AUTOMOC OFF
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)
target_include_directories(test_fomoddependency PRIVATE
    ${CMAKE_SOURCE_DIR}/libs/installer_fomod/src
)
target_link_libraries(test_fomoddependency PRIVATE
    Qt6::Core
    GTest::gtest
    GTest::gtest_main
)
add_test(NAME test_fomoddependency COMMAND test_fomoddependency)

add_executable(test_fomodpaths EXCLUDE_FROM_ALL test_fomodpaths.cpp)
set_target_properties(test_fomodpaths PROPERTIES
    AUTOMOC OFF
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)
target_include_directories(test_fomodpaths PRIVATE
    ${CMAKE_SOURCE_DIR}/libs/installer_fomod/src
)
target_link_libraries(test_fomodpaths PRIVATE
    Qt6::Core
    GTest::gtest
    GTest::gtest_main
)
add_test(NAME test_fomodpaths COMMAND test_fomodpaths)

add_executable(test_registry_ini EXCLUDE_FROM_ALL test_registry_ini.cpp)
set_target_properties(test_registry_ini PROPERTIES AUTOMOC OFF CXX_STANDARD 23)
target_compile_options(test_registry_ini PRIVATE
    "-idirafter" "${CMAKE_SOURCE_DIR}/libs/uibase/include/uibase")
target_link_libraries(test_registry_ini PRIVATE
    mo2::uibase Qt6::Widgets GTest::gtest_main)
add_test(NAME test_registry_ini COMMAND test_registry_ini)

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
# Native FUSE mount: existing mod files are written in place (MO2 behavior),
# base-game files are copy-on-write. Skips itself when FUSE can't mount.
# ---------------------------------------------------------------------------
add_executable(test_vfs_inplace_writes EXCLUDE_FROM_ALL
    test_vfs_inplace_writes.cpp
    ${CMAKE_SOURCE_DIR}/src/src/vfs/mo2filesystem.cpp
    ${CMAKE_SOURCE_DIR}/src/src/vfs/inodetable.cpp
    ${CMAKE_SOURCE_DIR}/src/src/vfs/openfilecleanup.cpp
    ${CMAKE_SOURCE_DIR}/src/src/vfs/overwritemanager.cpp
    ${CMAKE_SOURCE_DIR}/src/src/vfs/runtimeindex.cpp
    ${CMAKE_SOURCE_DIR}/src/src/vfs/vfstree.cpp
)
set_target_properties(test_vfs_inplace_writes PROPERTIES
    AUTOMOC OFF
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)
target_include_directories(test_vfs_inplace_writes PRIVATE
    ${CMAKE_SOURCE_DIR}/src/src
)
target_compile_definitions(test_vfs_inplace_writes PRIVATE
    FUSE_USE_VERSION=312
)
target_link_libraries(test_vfs_inplace_writes PRIVATE
    PkgConfig::TEST_FUSE3
    GTest::gtest
    GTest::gtest_main
)
add_test(NAME test_vfs_inplace_writes COMMAND test_vfs_inplace_writes)

# ---------------------------------------------------------------------------
# FUSE mount path alias detection used by stale-mount recovery
# ---------------------------------------------------------------------------
add_executable(test_mount_path_utils EXCLUDE_FROM_ALL
    test_mount_path_utils.cpp
    ${CMAKE_SOURCE_DIR}/src/src/mountpathutils.cpp
)
set_target_properties(test_mount_path_utils PROPERTIES
    AUTOMOC OFF
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)
target_include_directories(test_mount_path_utils PRIVATE
    ${CMAKE_SOURCE_DIR}/src/src
)
target_link_libraries(test_mount_path_utils PRIVATE
    Qt6::Core
    GTest::gtest
    GTest::gtest_main
)
add_test(NAME test_mount_path_utils COMMAND test_mount_path_utils)

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
# Shared NXM authorization routing for CLF3 and curated-guide downloads
# ---------------------------------------------------------------------------
add_executable(test_clf3_nxm_broker EXCLUDE_FROM_ALL
    test_clf3_nxm_broker.cpp
    ${CMAKE_SOURCE_DIR}/src/src/curatedguidenxmbroker.cpp
)
set_target_properties(test_clf3_nxm_broker PROPERTIES
    AUTOMOC ON
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)
target_include_directories(test_clf3_nxm_broker PRIVATE
    ${CMAKE_SOURCE_DIR}/libs/uibase/include
)
target_link_libraries(test_clf3_nxm_broker PRIVATE
    Qt6::Core
    mo2::uibase
    GTest::gtest
    GTest::gtest_main
)
add_test(NAME test_clf3_nxm_broker COMMAND test_clf3_nxm_broker)

# ---------------------------------------------------------------------------
# Embedded-Python package replacement across in-process instance switches
# ---------------------------------------------------------------------------
if(TARGET runner AND TARGET mobase)
add_executable(test_python_module_switch EXCLUDE_FROM_ALL
    ${CMAKE_SOURCE_DIR}/libs/plugin_python/tests/runner/main.cpp
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
)
add_test(NAME test_python_module_switch COMMAND test_python_module_switch)
set_tests_properties(test_python_module_switch PROPERTIES
    ENVIRONMENT
        "PLUGIN_DIR=${CMAKE_SOURCE_DIR}/libs/plugin_python/tests/runner/plugins;PYTHONPATH=$<TARGET_FILE_DIR:mobase>"
)
endif()

# CLF3 process lifecycle, preflight, setup recovery, and log redaction.
find_package(Qt6 REQUIRED COMPONENTS Test)
add_executable(test_clf3_install EXCLUDE_FROM_ALL
    test_clf3_install.cpp
    test_clf3_engine.cpp
    test_clf3_gallery.cpp
    ${CMAKE_SOURCE_DIR}/src/src/clf3enginemanager.cpp
    ${CMAKE_SOURCE_DIR}/src/src/clf3galleryloader.cpp
    ${CMAKE_SOURCE_DIR}/src/src/clf3processcontroller.cpp
    ${CMAKE_SOURCE_DIR}/src/src/clf3installutils.cpp
)
set_target_properties(test_clf3_install PROPERTIES
    AUTOMOC ON
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)
target_link_libraries(test_clf3_install PRIVATE Qt6::Core Qt6::Network Qt6::Test GTest::gtest)
add_test(NAME test_clf3_install COMMAND test_clf3_install)
set_tests_properties(test_clf3_install PROPERTIES TIMEOUT 90)

# ---------------------------------------------------------------------------
# Persistent cookie storage
# ---------------------------------------------------------------------------
add_executable(test_persistentcookiejar EXCLUDE_FROM_ALL
    test_persistentcookiejar.cpp
    ${CMAKE_SOURCE_DIR}/src/src/persistentcookiejar.cpp
    ${CMAKE_SOURCE_DIR}/src/src/persistentcookiejar.h
)
set_target_properties(test_persistentcookiejar PROPERTIES
    AUTOMOC ON
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)
target_include_directories(test_persistentcookiejar PRIVATE
    ${CMAKE_SOURCE_DIR}/src/src
)
target_link_libraries(test_persistentcookiejar PRIVATE
    Qt6::Core Qt6::Network mo2::uibase GTest::gtest
)
add_test(NAME test_persistentcookiejar COMMAND test_persistentcookiejar)
set_tests_properties(test_persistentcookiejar PROPERTIES TIMEOUT 10)

add_executable(test_fluorineupdater EXCLUDE_FROM_ALL
    test_fluorineupdater.cpp
    ${CMAKE_SOURCE_DIR}/src/src/fluorineupdater.cpp
    ${CMAKE_SOURCE_DIR}/src/src/fluorineupdater.h
)
set_target_properties(test_fluorineupdater PROPERTIES
    AUTOMOC ON
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)
target_include_directories(test_fluorineupdater PRIVATE
    ${CMAKE_SOURCE_DIR}/src/src
)
target_link_libraries(test_fluorineupdater PRIVATE
    Qt6::Core
    Qt6::Network
    mo2::uibase
    GTest::gtest
)
add_test(NAME test_fluorineupdater COMMAND test_fluorineupdater)
set_tests_properties(test_fluorineupdater PROPERTIES TIMEOUT 10)

add_executable(test_downloadmetadatapolicy EXCLUDE_FROM_ALL
    test_downloadmetadatapolicy.cpp
    ${CMAKE_SOURCE_DIR}/src/src/downloadmetadatapolicy.cpp
)
set_target_properties(test_downloadmetadatapolicy PROPERTIES
    AUTOMOC OFF
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)
target_include_directories(test_downloadmetadatapolicy PRIVATE
    ${CMAKE_SOURCE_DIR}/src/src
)
target_link_libraries(test_downloadmetadatapolicy PRIVATE
    Qt6::Core
    GTest::gtest
    GTest::gtest_main
)
add_test(NAME test_downloadmetadatapolicy COMMAND test_downloadmetadatapolicy)
set_tests_properties(test_downloadmetadatapolicy PROPERTIES TIMEOUT 10)


add_executable(test_downloadwritelifecycle EXCLUDE_FROM_ALL
    test_downloadwritelifecycle.cpp
)
set_target_properties(test_downloadwritelifecycle PROPERTIES
    AUTOMOC OFF
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)
target_include_directories(test_downloadwritelifecycle PRIVATE
    ${CMAKE_SOURCE_DIR}/src/src
)
target_link_libraries(test_downloadwritelifecycle PRIVATE
    Qt6::Core
    GTest::gtest
    GTest::gtest_main
)
add_test(NAME test_downloadwritelifecycle COMMAND test_downloadwritelifecycle)
set_tests_properties(test_downloadwritelifecycle PROPERTIES TIMEOUT 10)

add_executable(test_downloadresponse EXCLUDE_FROM_ALL
    test_downloadresponse.cpp)
set_target_properties(test_downloadresponse PROPERTIES
    AUTOMOC OFF
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON)
target_include_directories(test_downloadresponse PRIVATE
    ${CMAKE_SOURCE_DIR}/src/src)
target_link_libraries(test_downloadresponse PRIVATE
    Qt6::Core
    GTest::gtest
    GTest::gtest_main)
add_test(NAME test_downloadresponse COMMAND test_downloadresponse)
set_tests_properties(test_downloadresponse PROPERTIES TIMEOUT 10)


# Register a "fluorine-tests" umbrella target that builds every test exe.
add_executable(test_executable_options EXCLUDE_FROM_ALL
    test_executable_options.cpp
    ${CMAKE_SOURCE_DIR}/src/src/executableslist.cpp
    ${CMAKE_SOURCE_DIR}/src/src/launchenvironment.cpp
)
set_target_properties(test_executable_options PROPERTIES
    AUTOMOC OFF
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)
target_include_directories(test_executable_options PRIVATE
    ${CMAKE_SOURCE_DIR}/src/src
)
# Exercise the executable model without linking the organizer UI and settings
# machinery used by the other methods in executableslist.cpp.
target_compile_options(test_executable_options PRIVATE
    -ffunction-sections -fdata-sections
    "-idirafter" "${CMAKE_SOURCE_DIR}/libs/uibase/include/uibase"
)
target_precompile_headers(test_executable_options PRIVATE <QtWidgets> <QSettings>)
target_link_options(test_executable_options PRIVATE -Wl,--gc-sections)
target_link_libraries(test_executable_options PRIVATE
    Qt6::Core mo2::uibase GTest::gtest GTest::gtest_main
)
add_test(NAME test_executable_options COMMAND test_executable_options)

add_executable(test_launch_environment EXCLUDE_FROM_ALL
    test_launch_environment.cpp
    ${CMAKE_SOURCE_DIR}/src/src/nativeprocess.cpp
    ${CMAKE_SOURCE_DIR}/src/src/processlifetime.cpp
    ${CMAKE_SOURCE_DIR}/src/src/faudioruntime.cpp
    ${CMAKE_SOURCE_DIR}/src/src/protonlauncher.cpp
    ${CMAKE_SOURCE_DIR}/src/src/launchenvironment.cpp
    ${CMAKE_SOURCE_DIR}/src/src/vfsbackend.cpp)
find_package(Boost CONFIG REQUIRED COMPONENTS program_options)
set_target_properties(test_launch_environment PROPERTIES AUTOMOC OFF CXX_STANDARD 23)
target_include_directories(test_launch_environment PRIVATE ${CMAKE_SOURCE_DIR}/src/src)
target_compile_options(test_launch_environment PRIVATE
    "-idirafter" "${CMAKE_SOURCE_DIR}/libs/uibase/include/uibase")
target_link_libraries(test_launch_environment PRIVATE
    Qt6::Core mo2::uibase Boost::program_options GTest::gtest)
add_test(NAME test_launch_environment COMMAND test_launch_environment)

# Native child ownership and plugin process-token lifecycle.
find_package(Threads REQUIRED)
add_executable(test_nativeprocess EXCLUDE_FROM_ALL
    test_nativeprocess.cpp
    ${CMAKE_SOURCE_DIR}/src/src/nativeprocess.cpp)
set_target_properties(test_nativeprocess PROPERTIES AUTOMOC OFF CXX_STANDARD 23)
target_include_directories(test_nativeprocess PRIVATE ${CMAKE_SOURCE_DIR}/src/src)
target_link_libraries(test_nativeprocess PRIVATE Qt6::Core GTest::gtest Threads::Threads)
add_test(NAME test_nativeprocess COMMAND test_nativeprocess)
set_tests_properties(test_nativeprocess PROPERTIES TIMEOUT 30)

add_executable(test_pluginprocessregistry EXCLUDE_FROM_ALL
    test_pluginprocessregistry.cpp
    ${CMAKE_SOURCE_DIR}/src/src/nativeprocess.cpp)
set_target_properties(test_pluginprocessregistry PROPERTIES AUTOMOC OFF CXX_STANDARD 23)
target_include_directories(test_pluginprocessregistry PRIVATE ${CMAKE_SOURCE_DIR}/src/src)
target_link_libraries(test_pluginprocessregistry PRIVATE
    Qt6::Core GTest::gtest GTest::gtest_main Threads::Threads)
add_test(NAME test_pluginprocessregistry COMMAND test_pluginprocessregistry)

add_executable(test_faudioruntime EXCLUDE_FROM_ALL
    test_faudioruntime.cpp
    ${CMAKE_SOURCE_DIR}/src/src/faudioruntime.cpp)
set_target_properties(test_faudioruntime PROPERTIES AUTOMOC OFF CXX_STANDARD 23)
target_include_directories(test_faudioruntime PRIVATE ${CMAKE_SOURCE_DIR}/src/src)
target_link_libraries(test_faudioruntime PRIVATE Qt6::Core GTest::gtest GTest::gtest_main)
add_test(NAME test_faudioruntime COMMAND test_faudioruntime)

add_executable(test_conflict_sorting EXCLUDE_FROM_ALL
    test_conflict_sorting.cpp
    ${CMAKE_SOURCE_DIR}/src/src/modinfodialogconflictsmodels.cpp
    ${CMAKE_SOURCE_DIR}/src/src/modinfodialogconflictsmodels.h)
set_target_properties(test_conflict_sorting PROPERTIES AUTOMOC ON CXX_STANDARD 23)
target_include_directories(test_conflict_sorting PRIVATE ${CMAKE_SOURCE_DIR}/src/src)
target_compile_options(test_conflict_sorting PRIVATE -ffunction-sections -fdata-sections
    "-idirafter" "${CMAKE_SOURCE_DIR}/libs/uibase/include/uibase")
target_precompile_headers(test_conflict_sorting PRIVATE <QtWidgets> <QSettings>
    <boost/shared_ptr.hpp> <uibase/windows_compat.h>)
target_link_options(test_conflict_sorting PRIVATE -Wl,--gc-sections)
target_link_libraries(test_conflict_sorting PRIVATE Qt6::Widgets mo2::uibase GTest::gtest)
add_test(NAME test_conflict_sorting COMMAND test_conflict_sorting)
set_tests_properties(test_conflict_sorting PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

find_package(Qt6 REQUIRED COMPONENTS Widgets WebSockets Network)
add_executable(test_steam_cloud EXCLUDE_FROM_ALL
    test_steam_cloud.cpp
    ${CMAKE_SOURCE_DIR}/src/src/steamcloudsync.cpp
    ${CMAKE_SOURCE_DIR}/src/src/vdfparser.cpp
)
set_target_properties(test_steam_cloud PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)
target_link_libraries(test_steam_cloud PRIVATE Qt6::Widgets Qt6::Network Qt6::WebSockets GTest::gtest)
add_test(NAME test_steam_cloud COMMAND test_steam_cloud)
set_tests_properties(test_steam_cloud PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

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
# Canonical and legacy instance [General] key mapping
# ---------------------------------------------------------------------------
add_executable(test_instancegeneralsettings EXCLUDE_FROM_ALL
    test_instancegeneralsettings.cpp
    ${CMAKE_SOURCE_DIR}/src/src/instancegeneralsettings.cpp
)
set_target_properties(test_instancegeneralsettings PROPERTIES
    AUTOMOC OFF
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)
target_include_directories(test_instancegeneralsettings PRIVATE
    ${CMAKE_SOURCE_DIR}/src/src
)
target_link_libraries(test_instancegeneralsettings PRIVATE
    Qt6::Core
    GTest::gtest
    GTest::gtest_main
)
add_test(NAME test_instancegeneralsettings COMMAND test_instancegeneralsettings)
set_tests_properties(test_instancegeneralsettings PROPERTIES TIMEOUT 10)

add_executable(test_desktopshortcutpolicy EXCLUDE_FROM_ALL
    test_desktopshortcutpolicy.cpp)
set_target_properties(test_desktopshortcutpolicy PROPERTIES
    AUTOMOC OFF CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)
target_include_directories(test_desktopshortcutpolicy PRIVATE
    ${CMAKE_SOURCE_DIR}/src/src)
target_link_libraries(test_desktopshortcutpolicy PRIVATE
    Qt6::Core GTest::gtest GTest::gtest_main)
add_test(NAME test_desktopshortcutpolicy COMMAND test_desktopshortcutpolicy)

add_executable(test_desktopshortcut EXCLUDE_FROM_ALL
    test_desktopshortcut.cpp ${CMAKE_SOURCE_DIR}/src/src/envshortcut.cpp)
set_target_properties(test_desktopshortcut PROPERTIES
    AUTOMOC OFF CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)
target_include_directories(test_desktopshortcut PRIVATE ${CMAKE_SOURCE_DIR}/src/src)
target_compile_options(test_desktopshortcut PRIVATE -ffunction-sections -fdata-sections
    "-idirafter" "${CMAKE_SOURCE_DIR}/libs/uibase/include/uibase")
target_precompile_headers(test_desktopshortcut PRIVATE <QtWidgets> <QSettings>
    <boost/shared_ptr.hpp> <uibase/windows_compat.h>)
target_link_options(test_desktopshortcut PRIVATE -Wl,--gc-sections)
target_link_libraries(test_desktopshortcut PRIVATE Qt6::Widgets mo2::uibase GTest::gtest)
add_test(NAME test_desktopshortcut COMMAND test_desktopshortcut)
set_tests_properties(test_desktopshortcut PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_test(NAME test_desktop_portable_policy
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_desktop_portable_policy.py)
add_test(NAME test_executable_shortcut_surface
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_executable_shortcut_surface.py)

add_executable(test_logredaction EXCLUDE_FROM_ALL test_logredaction.cpp)
set_target_properties(test_logredaction PROPERTIES
    AUTOMOC OFF CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)
target_link_libraries(test_logredaction PRIVATE
    Qt6::Core mo2::uibase GTest::gtest GTest::gtest_main)
add_test(NAME test_logredaction COMMAND test_logredaction)
set_tests_properties(test_logredaction PROPERTIES TIMEOUT 10)

add_executable(test_nexuscachedirectory EXCLUDE_FROM_ALL
    test_nexuscachedirectory.cpp
    ${CMAKE_SOURCE_DIR}/src/src/nexuscachedirectory.cpp)
set_target_properties(test_nexuscachedirectory PROPERTIES
    AUTOMOC OFF CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)
target_include_directories(test_nexuscachedirectory PRIVATE ${CMAKE_SOURCE_DIR}/src/src)
target_link_libraries(test_nexuscachedirectory PRIVATE
    Qt6::Core Qt6::Network GTest::gtest)
add_test(NAME test_nexuscachedirectory COMMAND test_nexuscachedirectory)
set_tests_properties(test_nexuscachedirectory PROPERTIES TIMEOUT 10)
add_test(NAME test_nexus_cache_surface
    COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/test_nexus_cache_surface.py)

add_executable(test_problemcheckrunner EXCLUDE_FROM_ALL test_problemcheckrunner.cpp)
set_target_properties(test_problemcheckrunner PROPERTIES
    AUTOMOC OFF CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)
target_include_directories(test_problemcheckrunner PRIVATE ${CMAKE_SOURCE_DIR}/src/src)
target_link_libraries(test_problemcheckrunner PRIVATE Qt6::Core GTest::gtest GTest::gtest_main)
add_test(NAME test_problemcheckrunner COMMAND test_problemcheckrunner)
set_tests_properties(test_problemcheckrunner PROPERTIES TIMEOUT 10)

add_executable(test_recenterrorlog EXCLUDE_FROM_ALL
    test_recenterrorlog.cpp
    ${CMAKE_SOURCE_DIR}/libs/diagnose_basic/src/recenterrorlog.cpp)
set_target_properties(test_recenterrorlog PROPERTIES
    AUTOMOC OFF CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)
target_include_directories(test_recenterrorlog PRIVATE ${CMAKE_SOURCE_DIR}/libs/diagnose_basic/src)
target_link_libraries(test_recenterrorlog PRIVATE Qt6::Core GTest::gtest GTest::gtest_main)
add_test(NAME test_recenterrorlog COMMAND test_recenterrorlog)
set_tests_properties(test_recenterrorlog PROPERTIES TIMEOUT 10)

add_executable(test_game_artwork EXCLUDE_FROM_ALL
    test_game_artwork.cpp
    ${CMAKE_SOURCE_DIR}/src/src/gameartwork.cpp
    ${CMAKE_SOURCE_DIR}/src/src/gameartwork.h
    ${CMAKE_SOURCE_DIR}/src/src/gamelibrarywidget.cpp
    ${CMAKE_SOURCE_DIR}/src/src/gameartwork.qrc
    ${CMAKE_SOURCE_DIR}/libs/uibase/src/steamutility.cpp)
set_target_properties(test_game_artwork PROPERTIES AUTOMOC ON AUTORCC ON CXX_STANDARD 23)
target_include_directories(test_game_artwork PRIVATE
    ${CMAKE_SOURCE_DIR}/src/src
    ${CMAKE_SOURCE_DIR}/libs/uibase/include)
target_compile_options(test_game_artwork PRIVATE
    "-idirafter" "${CMAKE_SOURCE_DIR}/libs/uibase/include/uibase")
target_link_libraries(test_game_artwork PRIVATE Qt6::Widgets Qt6::Network GTest::gtest)
add_test(NAME test_game_artwork COMMAND test_game_artwork)
set_tests_properties(test_game_artwork PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(test_settings_navigation EXCLUDE_FROM_ALL
    test_settings_navigation.cpp
    ${CMAKE_SOURCE_DIR}/src/src/settingsnavigation.cpp
    ${CMAKE_SOURCE_DIR}/src/src/settingsnavigation.h)
set_target_properties(test_settings_navigation PROPERTIES AUTOMOC ON CXX_STANDARD 23)
target_include_directories(test_settings_navigation PRIVATE ${CMAKE_SOURCE_DIR}/src/src)
target_link_libraries(test_settings_navigation PRIVATE Qt6::Widgets GTest::gtest)
add_test(NAME test_settings_navigation COMMAND test_settings_navigation)
set_tests_properties(test_settings_navigation PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(test_fluorine_menu_bar EXCLUDE_FROM_ALL
    test_fluorine_menu_bar.cpp
    ${CMAKE_SOURCE_DIR}/src/src/fluorinemenubar.cpp)
set_target_properties(test_fluorine_menu_bar PROPERTIES AUTOMOC OFF CXX_STANDARD 23)
target_include_directories(test_fluorine_menu_bar PRIVATE ${CMAKE_SOURCE_DIR}/src/src)
target_link_libraries(test_fluorine_menu_bar PRIVATE Qt6::Widgets Qt6::Test GTest::gtest)
add_test(NAME test_fluorine_menu_bar COMMAND test_fluorine_menu_bar)
set_tests_properties(test_fluorine_menu_bar PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(test_library_setup_panel EXCLUDE_FROM_ALL
    test_library_setup_panel.cpp
    ${CMAKE_SOURCE_DIR}/src/src/librarysetuppanel.cpp
    ${CMAKE_SOURCE_DIR}/src/src/librarysetuppanel.h
    ${CMAKE_SOURCE_DIR}/src/src/modlistmetadata.cpp
    ${CMAKE_SOURCE_DIR}/src/src/gameartwork.cpp
    ${CMAKE_SOURCE_DIR}/src/src/gameartwork.h
    ${CMAKE_SOURCE_DIR}/src/src/settingsnavigation.cpp
    ${CMAKE_SOURCE_DIR}/src/src/settingsnavigation.h
    ${CMAKE_SOURCE_DIR}/src/src/gameartwork.qrc
    ${CMAKE_SOURCE_DIR}/libs/uibase/src/steamutility.cpp)
set_target_properties(test_library_setup_panel PROPERTIES AUTOMOC ON AUTORCC ON CXX_STANDARD 23)
target_include_directories(test_library_setup_panel PRIVATE
    ${CMAKE_SOURCE_DIR}/src/src ${CMAKE_SOURCE_DIR}/libs/uibase/include)
target_compile_options(test_library_setup_panel PRIVATE
    "-idirafter" "${CMAKE_SOURCE_DIR}/libs/uibase/include/uibase")
target_link_libraries(test_library_setup_panel PRIVATE Qt6::Widgets Qt6::Network GTest::gtest)
add_test(NAME test_library_setup_panel COMMAND test_library_setup_panel)
set_tests_properties(test_library_setup_panel PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(test_openmw_settings EXCLUDE_FROM_ALL
    test_openmw_settings.cpp
    ${CMAKE_SOURCE_DIR}/src/src/openmwsettings.cpp)
set_target_properties(test_openmw_settings PROPERTIES CXX_STANDARD 23)
target_compile_definitions(test_openmw_settings PRIVATE
    OPENMW_DEFAULTS_PATH="${CMAKE_SOURCE_DIR}/src/src/openmw-settings-default.cfg")
target_link_libraries(test_openmw_settings PRIVATE Qt6::Core GTest::gtest GTest::gtest_main)
add_test(NAME test_openmw_settings COMMAND test_openmw_settings)

add_executable(test_openmw_settings_tab EXCLUDE_FROM_ALL
    test_openmw_settings_tab.cpp
    ${CMAKE_SOURCE_DIR}/src/src/openmwsettings.cpp
    ${CMAKE_SOURCE_DIR}/src/src/openmwsettingstab.cpp
    ${CMAKE_SOURCE_DIR}/src/src/openmwsettings.qrc)
set_target_properties(test_openmw_settings_tab PROPERTIES AUTOMOC ON AUTORCC ON CXX_STANDARD 23)
target_include_directories(test_openmw_settings_tab PRIVATE ${CMAKE_SOURCE_DIR}/src/src)
target_compile_definitions(test_openmw_settings_tab PRIVATE
    OPENMW_SCREENSHOT_DIR="${CMAKE_BINARY_DIR}/openmw-settings-screenshots")
target_link_libraries(test_openmw_settings_tab PRIVATE Qt6::Widgets Qt6::Test GTest::gtest GTest::gtest_main)
add_test(NAME test_openmw_settings_tab COMMAND test_openmw_settings_tab)
set_tests_properties(test_openmw_settings_tab PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_custom_target(fluorine-tests
    DEPENDS test_stylesheetpath test_applicationappearance test_plugindatapath
            test_instancegeneralsettings test_desktopshortcutpolicy test_desktopshortcut
            test_downloadmetadatapolicy test_downloadwritelifecycle test_downloadresponse
            test_logredaction test_nexuscachedirectory test_problemcheckrunner test_recenterrorlog
            test_nativeprocess test_pluginprocessregistry
            test_game_artwork test_settings_navigation test_fluorine_menu_bar
            test_library_setup_panel
            test_esptk_tes3 test_falloutnv_plugin_types test_bsatk_extraction test_executable_options test_steam_cloud
            test_conflict_sorting test_launch_environment test_faudioruntime
            test_plugincompatibility test_pefileutils test_curatedguide
            test_metainiutils test_fomoddependency test_fomodpaths test_registry_ini test_persistentcookiejar test_fluorineupdater
            test_vfs_backend
            test_fluorineconfigownership
            test_prefixsymlinks
            test_heroicjson test_protondetection
            test_external_dir_mapping test_portablelauncherscript test_xdgpaths
            test_vfs_file_ops
            test_shell_operations
            test_vfs_descriptor_cleanup test_vfs_inplace_writes test_mount_path_utils
            test_vfs_runtime_index
            test_gamebryo_save_compression
            test_game_save_migration test_gamepath
            test_vfs_catalog test_vfs_index
            test_staging_promotion test_clf3_nxm_broker test_clf3_install
            test_openmw_settings test_openmw_settings_tab)
    if(TARGET test_python_module_switch)
        add_dependencies(fluorine-tests test_python_module_switch)
    endif()

# Collections acquisition containment and the actual Qt review/install workflow.
find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent)
add_executable(test_clf3_collections EXCLUDE_FROM_ALL
    test_clf3_collections.cpp
    ${CMAKE_SOURCE_DIR}/src/src/clf3collectioncompat.cpp
    ${CMAKE_SOURCE_DIR}/src/src/clf3collectionhost.cpp
    ${CMAKE_SOURCE_DIR}/src/src/clf3installertabs.cpp
    ${CMAKE_SOURCE_DIR}/src/src/clf3momwpage.cpp
    ${CMAKE_SOURCE_DIR}/src/src/clf3collectiondialog.cpp
    ${CMAKE_SOURCE_DIR}/src/src/clf3processcontroller.cpp
    ${CMAKE_SOURCE_DIR}/src/src/clf3enginemanager.cpp
    ${CMAKE_SOURCE_DIR}/src/src/clf3installutils.cpp
    ${CMAKE_SOURCE_DIR}/src/src/curatedguidenxmbroker.cpp
)
set_target_properties(test_clf3_collections PROPERTIES AUTOMOC ON CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)
target_include_directories(test_clf3_collections PRIVATE ${CMAKE_SOURCE_DIR}/libs/uibase/include)
target_link_libraries(test_clf3_collections PRIVATE mo2::uibase Qt6::Widgets Qt6::Network Qt6::Concurrent Qt6::Test GTest::gtest)
add_test(NAME test_clf3_collections COMMAND test_clf3_collections)
set_tests_properties(test_clf3_collections PROPERTIES TIMEOUT 180 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
add_dependencies(fluorine-tests test_clf3_collections)

# Match the real Qt progress widget while drawing download cells without
# constructing a widget for each paint.
add_executable(test_downloadprogressrenderer EXCLUDE_FROM_ALL
    test_downloadprogressrenderer.cpp)
set_target_properties(test_downloadprogressrenderer PROPERTIES
    AUTOMOC OFF CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)
target_include_directories(test_downloadprogressrenderer PRIVATE
    ${CMAKE_SOURCE_DIR}/src/src)
target_link_libraries(test_downloadprogressrenderer PRIVATE
    Qt6::Widgets GTest::gtest GTest::gtest_main)
add_test(NAME test_downloadprogressrenderer COMMAND test_downloadprogressrenderer)
set_tests_properties(test_downloadprogressrenderer PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
add_dependencies(fluorine-tests test_downloadprogressrenderer)

# Basic setup checks use temporary files and emit navigation requests without
# launching a game or initializing a real prefix.
add_executable(test_setupchecks EXCLUDE_FROM_ALL
    test_setupchecks.cpp
    ${CMAKE_SOURCE_DIR}/src/src/setupchecks.cpp
    ${CMAKE_SOURCE_DIR}/src/src/setupchecks.h)
set_target_properties(test_setupchecks PROPERTIES
    AUTOMOC ON CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)
target_include_directories(test_setupchecks PRIVATE ${CMAKE_SOURCE_DIR}/src/src)
target_link_libraries(test_setupchecks PRIVATE Qt6::Widgets GTest::gtest)
add_test(NAME test_setupchecks COMMAND test_setupchecks)
set_tests_properties(test_setupchecks PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
add_dependencies(fluorine-tests test_setupchecks)

# Background reconciliation owns worker lifetime and cooperative cancellation.
add_executable(test_vfscatalogjob EXCLUDE_FROM_ALL
    test_vfscatalogjob.cpp
    ${CMAKE_SOURCE_DIR}/src/src/vfscatalogjob.cpp
    ${CMAKE_SOURCE_DIR}/src/src/vfscatalogjob.h)
set_target_properties(test_vfscatalogjob PROPERTIES AUTOMOC ON CXX_STANDARD 23)
target_include_directories(test_vfscatalogjob PRIVATE ${CMAKE_SOURCE_DIR}/src/src)
target_link_libraries(test_vfscatalogjob PRIVATE Qt6::Core Qt6::Concurrent GTest::gtest)
add_test(NAME test_vfscatalogjob COMMAND test_vfscatalogjob)
set_tests_properties(test_vfscatalogjob PROPERTIES TIMEOUT 30)
add_dependencies(fluorine-tests test_vfscatalogjob)

# Root locator recovery must preserve unrelated game-root entries.
add_executable(test_rootlocatordeployment EXCLUDE_FROM_ALL
    test_rootlocatordeployment.cpp
    ${CMAKE_SOURCE_DIR}/src/src/vfs/rootlocatordeployment.cpp)
set_target_properties(test_rootlocatordeployment PROPERTIES
    AUTOMOC OFF CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)
target_include_directories(test_rootlocatordeployment PRIVATE
    ${CMAKE_SOURCE_DIR}/src/src)
target_link_libraries(test_rootlocatordeployment PRIVATE
    Qt6::Core GTest::gtest GTest::gtest_main)
add_test(NAME test_rootlocatordeployment COMMAND test_rootlocatordeployment)
set_tests_properties(test_rootlocatordeployment PROPERTIES TIMEOUT 10)
add_dependencies(fluorine-tests test_rootlocatordeployment)

add_executable(test_fileconflictinspector EXCLUDE_FROM_ALL
    test_fileconflictinspector.cpp
    ${CMAKE_SOURCE_DIR}/src/src/fileconflictinspector.cpp)
set_target_properties(test_fileconflictinspector PROPERTIES AUTOMOC OFF CXX_STANDARD 23)
target_include_directories(test_fileconflictinspector PRIVATE ${CMAKE_SOURCE_DIR}/src/src)
target_link_libraries(test_fileconflictinspector PRIVATE Qt6::Widgets GTest::gtest)
add_test(NAME test_fileconflictinspector COMMAND test_fileconflictinspector)
set_tests_properties(test_fileconflictinspector PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
add_dependencies(fluorine-tests test_fileconflictinspector)

add_executable(test_diagnosticreport EXCLUDE_FROM_ALL
    test_diagnosticreport.cpp
    ${CMAKE_SOURCE_DIR}/src/src/diagnosticreport.cpp
    ${CMAKE_SOURCE_DIR}/src/src/diagnosticreport.h
    ${CMAKE_SOURCE_DIR}/src/src/vfs/vfsindex.cpp
    ${CMAKE_SOURCE_DIR}/src/src/vfs/vfstree.cpp)
set_target_properties(test_diagnosticreport PROPERTIES AUTOMOC ON CXX_STANDARD 23)
target_include_directories(test_diagnosticreport PRIVATE ${CMAKE_SOURCE_DIR}/src/src)
target_link_libraries(test_diagnosticreport PRIVATE
    Qt6::Widgets Qt6::Test SQLite::SQLite3 BLAKE3::blake3 utf8proc GTest::gtest)
add_test(NAME test_diagnosticreport COMMAND test_diagnosticreport)
set_tests_properties(test_diagnosticreport PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
add_dependencies(fluorine-tests test_diagnosticreport)

include(${CMAKE_CURRENT_SOURCE_DIR}/preview_tests.cmake)

# Native workspace presentation and transitions, using the production widgets.
add_executable(test_workspace_layout EXCLUDE_FROM_ALL
    test_workspace_layout.cpp
    ${CMAKE_SOURCE_DIR}/src/src/workspacelayout.cpp)
set_target_properties(test_workspace_layout PROPERTIES CXX_STANDARD 23)
target_include_directories(test_workspace_layout PRIVATE ${CMAKE_SOURCE_DIR}/src/src)
target_link_libraries(test_workspace_layout PRIVATE Qt6::Widgets GTest::gtest)
add_test(NAME test_workspace_layout COMMAND test_workspace_layout)
set_tests_properties(test_workspace_layout PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
add_dependencies(fluorine-tests test_workspace_layout)

add_executable(test_selectedmodpanel EXCLUDE_FROM_ALL
    test_selectedmodpanel.cpp
    ${CMAKE_SOURCE_DIR}/src/src/selectedmodpanel.cpp
    ${CMAKE_SOURCE_DIR}/src/src/selectedmodpanel.h)
set_target_properties(test_selectedmodpanel PROPERTIES AUTOMOC ON CXX_STANDARD 23)
target_include_directories(test_selectedmodpanel PRIVATE ${CMAKE_SOURCE_DIR}/src/src)
target_link_libraries(test_selectedmodpanel PRIVATE Qt6::Widgets Qt6::Test GTest::gtest)
add_test(NAME test_selectedmodpanel COMMAND test_selectedmodpanel)
set_tests_properties(test_selectedmodpanel PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
add_dependencies(fluorine-tests test_selectedmodpanel)

add_executable(test_workspaceframe EXCLUDE_FROM_ALL
    test_workspaceframe.cpp
    ${CMAKE_SOURCE_DIR}/src/src/workspaceframe.cpp
    ${CMAKE_SOURCE_DIR}/src/src/workspaceframe.h
    ${CMAKE_SOURCE_DIR}/src/src/selectedmodpanel.cpp
    ${CMAKE_SOURCE_DIR}/src/src/selectedmodpanel.h
    ${CMAKE_SOURCE_DIR}/src/src/fluorinetheme.cpp)
set_target_properties(test_workspaceframe PROPERTIES AUTOMOC ON CXX_STANDARD 23)
target_include_directories(test_workspaceframe PRIVATE ${CMAKE_SOURCE_DIR}/src/src)
target_link_libraries(test_workspaceframe PRIVATE Qt6::Widgets Qt6::Test GTest::gtest)
add_test(NAME test_workspaceframe COMMAND test_workspaceframe)
set_tests_properties(test_workspaceframe PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
add_dependencies(fluorine-tests test_workspaceframe)

# Header divider interaction and palette contrast.
add_executable(test_resizableheaderview EXCLUDE_FROM_ALL
    test_resizableheaderview.cpp
    ${CMAKE_SOURCE_DIR}/src/src/resizableheaderview.cpp
    ${CMAKE_SOURCE_DIR}/src/src/fluorinetheme.cpp)
set_target_properties(test_resizableheaderview PROPERTIES CXX_STANDARD 23)
target_include_directories(test_resizableheaderview PRIVATE ${CMAKE_SOURCE_DIR}/src/src)
target_link_libraries(test_resizableheaderview PRIVATE Qt6::Widgets Qt6::Test GTest::gtest)
add_test(NAME test_resizableheaderview COMMAND test_resizableheaderview)
set_tests_properties(test_resizableheaderview PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
add_dependencies(fluorine-tests test_resizableheaderview)

# Explicit opt-in visual/integration probe for a temporary packaged setup. No
# install rule: this helper is not part of the application distribution.
add_library(workspace_smoke_probe SHARED EXCLUDE_FROM_ALL workspace_smoke_probe.cpp)
target_link_libraries(workspace_smoke_probe PRIVATE Qt6::Widgets Qt6::Test)
set_target_properties(workspace_smoke_probe PROPERTIES CXX_STANDARD 23)
add_dependencies(fluorine-tests workspace_smoke_probe)

add_executable(test_installprogressview EXCLUDE_FROM_ALL
    test_installprogressview.cpp
    ${CMAKE_SOURCE_DIR}/src/src/installprogressview.cpp
    ${CMAKE_SOURCE_DIR}/src/src/installqueuemodel.cpp
    ${CMAKE_SOURCE_DIR}/src/src/fluorinetheme.cpp)
set_target_properties(test_installprogressview PROPERTIES AUTOMOC ON CXX_STANDARD 23)
target_include_directories(test_installprogressview PRIVATE ${CMAKE_SOURCE_DIR}/src/src)
target_link_libraries(test_installprogressview PRIVATE Qt6::Widgets Qt6::Test GTest::gtest)
add_test(NAME test_installprogressview COMMAND test_installprogressview)
set_tests_properties(test_installprogressview PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
add_dependencies(fluorine-tests test_installprogressview)

add_executable(test_modlistmetadata EXCLUDE_FROM_ALL
    test_modlistmetadata.cpp ${CMAKE_SOURCE_DIR}/src/src/modlistmetadata.cpp)
set_target_properties(test_modlistmetadata PROPERTIES AUTOMOC OFF CXX_STANDARD 23)
target_include_directories(test_modlistmetadata PRIVATE ${CMAKE_SOURCE_DIR}/src/src)
target_link_libraries(test_modlistmetadata PRIVATE Qt6::Core GTest::gtest)
add_test(NAME test_modlistmetadata COMMAND test_modlistmetadata)
add_dependencies(fluorine-tests test_modlistmetadata)

target_sources(test_clf3_collections PRIVATE ${CMAKE_SOURCE_DIR}/src/src/modlistmetadata.cpp)

add_library(installer_smoke_probe SHARED EXCLUDE_FROM_ALL installer_smoke_probe.cpp)
target_link_libraries(installer_smoke_probe PRIVATE Qt6::Widgets Qt6::Test)
set_target_properties(installer_smoke_probe PROPERTIES CXX_STANDARD 23)
add_dependencies(fluorine-tests installer_smoke_probe)

# Dialog integration is exercised only against a temporary packaged setup.
add_library(management_smoke_probe SHARED EXCLUDE_FROM_ALL management_smoke_probe.cpp)
target_link_libraries(management_smoke_probe PRIVATE Qt6::Widgets Qt6::Test)
set_target_properties(management_smoke_probe PROPERTIES CXX_STANDARD 23)
add_dependencies(fluorine-tests management_smoke_probe)

# Built with the test tooling; measured fixture runs are opt-in and are not CTest
# assertions or part of the shipped application.
add_executable(benchmark_vfs_cache_reuse EXCLUDE_FROM_ALL
    benchmark_vfs_cache_reuse.cpp
    ${CMAKE_SOURCE_DIR}/src/src/usvfssnapshot.cpp
    ${CMAKE_SOURCE_DIR}/src/src/fluorinepaths.cpp
    ${CMAKE_SOURCE_DIR}/src/src/fluorineconfig.cpp
    ${CMAKE_SOURCE_DIR}/src/src/vfs/vfscatalog.cpp
    ${CMAKE_SOURCE_DIR}/src/src/vfs/vfstree.cpp
    ${CMAKE_SOURCE_DIR}/src/src/vfs/vfsindex.cpp)
set_target_properties(benchmark_vfs_cache_reuse PROPERTIES
    AUTOMOC OFF CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)
target_include_directories(benchmark_vfs_cache_reuse PRIVATE
    ${CMAKE_SOURCE_DIR}/src/src
    ${CMAKE_SOURCE_DIR}/libs/uibase/include)
target_link_libraries(benchmark_vfs_cache_reuse PRIVATE
    Qt6::Core SQLite::SQLite3 BLAKE3::blake3 utf8proc mo2::uibase)
if(TARGET mo2::bsa_ffi)
    target_link_libraries(benchmark_vfs_cache_reuse PRIVATE mo2::bsa_ffi)
    target_compile_definitions(benchmark_vfs_cache_reuse PRIVATE FLUORINE_HAS_BSA_FFI=1)
endif()
add_dependencies(fluorine-tests benchmark_vfs_cache_reuse)

foreach(regression IN ITEMS moddirectorydiscovery)
    add_executable(test_${regression} EXCLUDE_FROM_ALL test_${regression}.cpp)
    set_target_properties(test_${regression} PROPERTIES AUTOMOC OFF CXX_STANDARD 23)
    target_include_directories(test_${regression} PRIVATE ${CMAKE_SOURCE_DIR}/src/src)
    target_link_libraries(test_${regression} PRIVATE Qt6::Core GTest::gtest GTest::gtest_main)
    add_test(NAME test_${regression} COMMAND test_${regression})
    add_dependencies(fluorine-tests test_${regression})
endforeach()

add_executable(test_credentialstore EXCLUDE_FROM_ALL
    test_credentialstore.cpp ${CMAKE_SOURCE_DIR}/src/src/credentialstore.cpp)
set_target_properties(test_credentialstore PROPERTIES AUTOMOC OFF CXX_STANDARD 23)
target_include_directories(test_credentialstore PRIVATE ${CMAKE_SOURCE_DIR}/src/src)
target_link_libraries(test_credentialstore PRIVATE Qt6::Core GTest::gtest GTest::gtest_main)
add_test(NAME test_credentialstore COMMAND test_credentialstore)
add_dependencies(fluorine-tests test_credentialstore)

add_executable(test_directorywalker EXCLUDE_FROM_ALL
    test_directorywalker.cpp ${CMAKE_SOURCE_DIR}/src/src/envfs.cpp)
set_target_properties(test_directorywalker PROPERTIES AUTOMOC OFF CXX_STANDARD 23)
target_include_directories(test_directorywalker PRIVATE ${CMAKE_SOURCE_DIR}/src/src)
target_compile_options(test_directorywalker PRIVATE -ffunction-sections -fdata-sections
    "-idirafter" "${CMAKE_SOURCE_DIR}/libs/uibase/include/uibase")
target_precompile_headers(test_directorywalker PRIVATE <QtWidgets> <uibase/utility.h>)
target_link_options(test_directorywalker PRIVATE -Wl,--gc-sections)
target_link_libraries(test_directorywalker PRIVATE Qt6::Core mo2::uibase GTest::gtest GTest::gtest_main)
add_test(NAME test_directorywalker COMMAND test_directorywalker)
add_dependencies(fluorine-tests test_directorywalker)

add_executable(test_lootmanager EXCLUDE_FROM_ALL
    test_lootmanager.cpp ${CMAKE_SOURCE_DIR}/src/src/lootmanager.cpp)
set_target_properties(test_lootmanager PROPERTIES AUTOMOC OFF CXX_STANDARD 23)
target_compile_definitions(test_lootmanager PRIVATE FLUORINE_LOOTMANAGER_TESTING)
target_include_directories(test_lootmanager PRIVATE ${CMAKE_SOURCE_DIR}/src/src)
target_link_libraries(test_lootmanager PRIVATE
    Qt6::Core Qt6::Network mo2::uibase GTest::gtest)
add_test(NAME test_lootmanager COMMAND test_lootmanager)
add_dependencies(fluorine-tests test_lootmanager)

add_executable(test_archive_extraction_containment EXCLUDE_FROM_ALL
    test_archive_extraction_containment.cpp)
set_target_properties(test_archive_extraction_containment PROPERTIES
    AUTOMOC OFF CXX_STANDARD 23)
target_compile_definitions(test_archive_extraction_containment PRIVATE
    FLUORINE_ARCHIVE_FIXTURE="${CMAKE_SOURCE_DIR}/src/tests/fixtures/archive_payload.zip")
target_link_libraries(test_archive_extraction_containment PRIVATE Qt6::Core mo2::archive GTest::gtest_main)
add_dependencies(test_archive_extraction_containment 7z)
add_test(NAME test_archive_extraction_containment COMMAND test_archive_extraction_containment)
set_tests_properties(test_archive_extraction_containment PROPERTIES
    ENVIRONMENT "MO2_LIBS_DIR=$<TARGET_FILE_DIR:7z>")
add_dependencies(fluorine-tests test_archive_extraction_containment)

add_test(NAME test_stalkeranomaly_save_fallback
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_stalkeranomaly_save_fallback.py)

# An in-process restart re-enters exec() and must not emit aboutToQuit.
add_executable(test_applicationexit EXCLUDE_FROM_ALL
    test_applicationexit.cpp)
set_target_properties(test_applicationexit PROPERTIES
    AUTOMOC OFF CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)
target_include_directories(test_applicationexit PRIVATE
    ${CMAKE_SOURCE_DIR}/src/src)
target_link_libraries(test_applicationexit PRIVATE
    Qt6::Core GTest::gtest GTest::gtest_main)
add_test(NAME test_applicationexit COMMAND test_applicationexit)
add_dependencies(fluorine-tests test_applicationexit)

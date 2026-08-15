cmake_minimum_required(VERSION 3.16)

find_package(Qt6 REQUIRED COMPONENTS
    Widgets WebSockets Network NetworkAuth Concurrent DBus)
option(MO2_ENABLE_WEBENGINE "Build optional Qt WebEngine browser support" OFF)
if(MO2_ENABLE_WEBENGINE)
    find_package(Qt6 QUIET COMPONENTS WebEngineWidgets)
endif()
find_package(Boost CONFIG REQUIRED COMPONENTS
    program_options)
find_package(spdlog CONFIG REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FUSE3 REQUIRED IMPORTED_TARGET fuse3)
find_package(Threads REQUIRED)
find_package(SQLite3 REQUIRED)

# Pinned official BLAKE3 C implementation. FetchContent keeps release builds
# reproducible without requiring a distro-specific development package.
include(FetchContent)
set(BUILD_TESTING_SAVED ${BUILD_TESTING})
set(BUILD_SHARED_LIBS_SAVED ${BUILD_SHARED_LIBS})
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(blake3_upstream
    URL https://github.com/BLAKE3-team/BLAKE3/archive/8aa5145039b972ba30e98e788752d37d14568824.tar.gz
    URL_HASH SHA256=4a4c27185216b9dd3219ce12045ec22cd7b264b51feae7eab9cdec9cf6e9de54
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR c)
FetchContent_MakeAvailable(blake3_upstream)

# Producer-independent Unicode normalization for the public VFS index wire
# format. The Windows consumer pins the same upstream revision.
FetchContent_Declare(utf8proc_upstream
    GIT_REPOSITORY https://github.com/JuliaStrings/utf8proc.git
    GIT_TAG a1b99daa2a3393884220264c927a48ba1251a9c6
    GIT_SHALLOW TRUE)
set(UTF8PROC_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(utf8proc_upstream)
set(BUILD_TESTING ${BUILD_TESTING_SAVED} CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS_SAVED} CACHE BOOL "" FORCE)

qt_standard_project_setup()

# Collect all source files
file(GLOB ORGANIZER_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/shared/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/vfs/*.cpp)

# These files are compiled into libuibase.so (shared with plugins) — exclude from organizer.
list(REMOVE_ITEM ORGANIZER_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/gamedetection.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/heroicjson.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/steamdetection.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/vdfparser.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/iconextractor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/prefixsymlinks.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/slrmanager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/steamappinfo.cpp)
file(GLOB ORGANIZER_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/shared/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/vfs/*.h)
file(GLOB ORGANIZER_UI
    ${CMAKE_CURRENT_SOURCE_DIR}/*.ui)
file(GLOB ORGANIZER_QRC
    ${CMAKE_CURRENT_SOURCE_DIR}/*.qrc)

# Keep explicitly listed sources tracked by CMake even when using GLOB.
list(APPEND ORGANIZER_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/commandlinearguments.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/downloadmetadatapolicy.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/externalmessagequeue.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/fluorineconfig.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/fluorinepaths.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/legacyflatpakmigration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/instancegeneralsettings.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/instancepathidentity.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lootmanager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/multiprocessprotocol.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/nexuscachedirectory.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/nxmhandlerintegration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/nxmrequest.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/prefixsetupdialog.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/prefixsetuprunner.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/profilemodlistrename.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/profilerename.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/protondxvkconfig.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/protonlauncher.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/runtimelock.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/settingsdialogproton.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/unixtermination.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/wineprofileinisync.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/winesavedeployment.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/winesaverouting.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/winesavetargetresolver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/wineregistryfile.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/winepluginlistsync.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/wineprefix.cpp)
list(APPEND ORGANIZER_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/commandlinearguments.h
    ${CMAKE_CURRENT_SOURCE_DIR}/downloadmetadatapolicy.h
    ${CMAKE_CURRENT_SOURCE_DIR}/externalmessagequeue.h
    ${CMAKE_CURRENT_SOURCE_DIR}/fluorineconfig.h
    ${CMAKE_CURRENT_SOURCE_DIR}/fluorinepaths.h
    ${CMAKE_CURRENT_SOURCE_DIR}/legacyflatpakmigration.h
    ${CMAKE_CURRENT_SOURCE_DIR}/instancegeneralsettings.h
    ${CMAKE_CURRENT_SOURCE_DIR}/instancepathidentity.h
    ${CMAKE_CURRENT_SOURCE_DIR}/lootmanager.h
    ${CMAKE_CURRENT_SOURCE_DIR}/multiprocessprotocol.h
    ${CMAKE_CURRENT_SOURCE_DIR}/nexuscachedirectory.h
    ${CMAKE_CURRENT_SOURCE_DIR}/nxmhandlerintegration.h
    ${CMAKE_CURRENT_SOURCE_DIR}/nxmrequest.h
    ${CMAKE_CURRENT_SOURCE_DIR}/prefixsetupdialog.h
    ${CMAKE_CURRENT_SOURCE_DIR}/prefixsetuprunner.h
    ${CMAKE_CURRENT_SOURCE_DIR}/profilemodlistrename.h
    ${CMAKE_CURRENT_SOURCE_DIR}/profilerename.h
    ${CMAKE_CURRENT_SOURCE_DIR}/protondxvkconfig.h
    ${CMAKE_CURRENT_SOURCE_DIR}/protonlauncher.h
    ${CMAKE_CURRENT_SOURCE_DIR}/runtimelock.h
    ${CMAKE_CURRENT_SOURCE_DIR}/settingsdialogproton.h
    ${CMAKE_CURRENT_SOURCE_DIR}/unixtermination.h
    ${CMAKE_CURRENT_SOURCE_DIR}/wineprofileinisync.h
    ${CMAKE_CURRENT_SOURCE_DIR}/winesavedeployment.h
    ${CMAKE_CURRENT_SOURCE_DIR}/winesaverouting.h
    ${CMAKE_CURRENT_SOURCE_DIR}/winesavetargetresolver.h
    ${CMAKE_CURRENT_SOURCE_DIR}/wineregistryfile.h
    ${CMAKE_CURRENT_SOURCE_DIR}/winepluginlistsync.h
    ${CMAKE_CURRENT_SOURCE_DIR}/wineprefix.h)


# Remove WebEngine-dependent sources when not available
if(NOT Qt6WebEngineWidgets_FOUND)
    list(REMOVE_ITEM ORGANIZER_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/browserview.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/browserdialog.cpp)
    list(REMOVE_ITEM ORGANIZER_HEADERS
        ${CMAKE_CURRENT_SOURCE_DIR}/browserview.h
        ${CMAKE_CURRENT_SOURCE_DIR}/browserdialog.h)
endif()

add_executable(organizer
    ${ORGANIZER_SOURCES}
    ${ORGANIZER_HEADERS}
    ${ORGANIZER_UI}
    ${ORGANIZER_QRC})

set_target_properties(organizer PROPERTIES
    OUTPUT_NAME "ModOrganizer"
    AUTOMOC ON
    AUTOUIC ON
    AUTORCC ON
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON)

# USVFS itself is a Windows DLL, so Fluorine hands a versioned launch request
# to a small Windows controller inside the same Proton invocation. Release
# builds provide the pinned runtime under /opt from docker/Dockerfile. Keep the
# native organizer build usable for contributors without MinGW or those assets.
find_program(FLUORINE_MINGW_CXX x86_64-w64-mingw32-g++)
set(FLUORINE_USVFS_RUNTIME_DIR "/opt/fluorine-usvfs" CACHE PATH
    "Directory containing the pinned USVFS Windows runtime")
set(FLUORINE_USVFS_REQUIRED_FILES
    usvfs_x64.dll
    usvfs_x86.dll
    usvfs_proxy_x64.exe
    usvfs_proxy_x86.exe)
set(FLUORINE_USVFS_RUNTIME_AVAILABLE TRUE)
foreach(runtime_file IN LISTS FLUORINE_USVFS_REQUIRED_FILES)
    if(NOT EXISTS "${FLUORINE_USVFS_RUNTIME_DIR}/${runtime_file}")
        set(FLUORINE_USVFS_RUNTIME_AVAILABLE FALSE)
    endif()
endforeach()

if(FLUORINE_MINGW_CXX AND FLUORINE_USVFS_RUNTIME_AVAILABLE)
    set(FLUORINE_USVFS_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/usvfs")
    set(FLUORINE_USVFS_HELPER
        "${FLUORINE_USVFS_OUTPUT_DIR}/fluorine-usvfs-launcher.exe")
    add_custom_command(
        OUTPUT "${FLUORINE_USVFS_HELPER}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${FLUORINE_USVFS_OUTPUT_DIR}"
        COMMAND "${FLUORINE_MINGW_CXX}"
                -std=c++20 -O2 -static -static-libgcc -static-libstdc++ -municode
                "${CMAKE_SOURCE_DIR}/tools/usvfs-launcher/main.cpp"
                -o "${FLUORINE_USVFS_HELPER}"
        DEPENDS "${CMAKE_SOURCE_DIR}/tools/usvfs-launcher/main.cpp"
        COMMENT "Building Wine-side Fluorine USVFS controller"
        VERBATIM)
    add_custom_target(fluorine-usvfs-runtime ALL
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${FLUORINE_USVFS_RUNTIME_DIR}/usvfs_x64.dll"
                "${FLUORINE_USVFS_RUNTIME_DIR}/usvfs_x86.dll"
                "${FLUORINE_USVFS_RUNTIME_DIR}/usvfs_proxy_x64.exe"
                "${FLUORINE_USVFS_RUNTIME_DIR}/usvfs_proxy_x86.exe"
                "${FLUORINE_USVFS_OUTPUT_DIR}"
        DEPENDS "${FLUORINE_USVFS_HELPER}"
        COMMENT "Refreshing pinned Wine-side USVFS runtime"
        VERBATIM)
    add_dependencies(organizer fluorine-usvfs-runtime)
else()
    message(WARNING
        "USVFS runtime helper will not be built (MinGW compiler or pinned runtime missing). "
        "The FUSE backend remains available.")
endif()

target_include_directories(organizer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
    # Generated Fluorine build-info header lives alongside other generated files.
    ${CMAKE_BINARY_DIR}/generated
    # Bare includes: source uses <iplugin*.h>, <log.h>, etc. without uibase/ prefix
    ${CMAKE_SOURCE_DIR}/libs/uibase/include/uibase
    ${CMAKE_SOURCE_DIR}/libs/uibase/include/uibase/game_features)

# Use precompiled header
target_precompile_headers(organizer PRIVATE pch.h)

target_link_libraries(organizer PRIVATE
    # Our sub-libraries
    mo2::uibase
    mo2::archive
    libbsarch
    libbsarch_OOP
    mo2::bsatk
    mo2::esptk
    # Qt6
    Qt6::Widgets
    Qt6::Concurrent
    $<$<TARGET_EXISTS:Qt6::WebEngineWidgets>:Qt6::WebEngineWidgets>
    Qt6::WebSockets
    Qt6::Network
    Qt6::NetworkAuth
    Qt6::DBus
    # Boost
    Boost::program_options
    # spdlog
    spdlog::spdlog_header_only
    # FUSE3 (low-level API)
    PkgConfig::FUSE3
    SQLite::SQLite3
    BLAKE3::blake3
    utf8proc
    # Linux system libs
    dl)

target_compile_definitions(organizer PRIVATE
    SPDLOG_USE_STD_FORMAT
    FUSE_USE_VERSION=312
    $<$<TARGET_EXISTS:Qt6::WebEngineWidgets>:MO2_WEBENGINE>)

if(TARGET mo2::bsa_ffi)
    target_link_libraries(organizer PRIVATE mo2::bsa_ffi)
    target_compile_definitions(organizer PRIVATE FLUORINE_HAS_BSA_FFI=1)
endif()

option(MO2_BUNDLE_7Z_RUNTIME "Copy a Linux 7z module into organizer/lib" ON)
option(MO2_STAGE_PYTHON_PLUGIN_PAYLOAD
    "Stage shipped Python plugin payload into build plugins/ for Linux runs" OFF)
option(MO2_STAGE_INSTALLER_WIZARD
    "Stage installer_wizard python plugin payload (requires additional wizard package)" OFF)
set(MO2_7Z_SO_PATH "" CACHE FILEPATH
    "Path to a compatible 7z shared module (exports CreateObject/GetHandlerProperty2)")
set(MO2_UPSTREAM_PLUGIN_PAYLOAD_DIR
    "$ENV{HOME}/Downloads/Mod.Organizer-2.5.2/plugins"
    CACHE PATH "Optional path to upstream MO2 plugins folder for pyCfg.py/data payload")

    if(MO2_BUNDLE_7Z_RUNTIME)
        if(TARGET 7z)
            # Use the 7z.so built from source by libs/7zip/CMakeLists.txt
            add_dependencies(organizer 7z)
            add_custom_command(TARGET organizer POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:organizer>/lib"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "$<TARGET_FILE:7z>" "$<TARGET_FILE_DIR:organizer>/lib/7z.so")
            message(STATUS "[MO2] Will bundle 7z.so built from source")
        else()
            message(WARNING
                "[MO2] MO2_BUNDLE_7Z_RUNTIME is ON but the 7z target was not found. "
                "Ensure libs/7zip is included in the build.")
        endif()
    endif()

    if(MO2_STAGE_PYTHON_PLUGIN_PAYLOAD)
        add_custom_command(TARGET organizer POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:organizer>/plugins/basic_games"
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:organizer>/plugins/data"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/libs/basic_games"
                "$<TARGET_FILE_DIR:organizer>/plugins/basic_games"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_SOURCE_DIR}/libs/preview_dds/src/DDSPreview.py"
                "${CMAKE_SOURCE_DIR}/libs/fnistool/src/FNISPatches.py"
                "${CMAKE_SOURCE_DIR}/libs/fnistool/src/FNISTool.py"
                "${CMAKE_SOURCE_DIR}/libs/fnistool/src/FNISToolReset.py"
                "${CMAKE_SOURCE_DIR}/libs/form43_checker/src/Form43Checker.py"
                "${CMAKE_SOURCE_DIR}/libs/script_extender_plugin_checker/src/ScriptExtenderPluginChecker.py"
                "${CMAKE_SOURCE_DIR}/libs/winreg.py"
                "${CMAKE_SOURCE_DIR}/libs/lzokay.py"
                "$<TARGET_FILE_DIR:organizer>/plugins"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/libs/preview_dds/src/DDS"
                "$<TARGET_FILE_DIR:organizer>/plugins/data/DDS"
        )

        if(MO2_STAGE_INSTALLER_WIZARD)
            add_custom_command(TARGET organizer POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:organizer>/plugins/installer_wizard"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CMAKE_SOURCE_DIR}/libs/installer_wizard/src/__init__.py"
                    "${CMAKE_SOURCE_DIR}/libs/installer_wizard/src/dialog.py"
                    "${CMAKE_SOURCE_DIR}/libs/installer_wizard/src/installer.py"
                    "${CMAKE_SOURCE_DIR}/libs/installer_wizard/src/runner.py"
                    "${CMAKE_SOURCE_DIR}/libs/installer_wizard/src/utils.py"
                    "$<TARGET_FILE_DIR:organizer>/plugins/installer_wizard"
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${CMAKE_SOURCE_DIR}/libs/installer_wizard/src/ui"
                    "$<TARGET_FILE_DIR:organizer>/plugins/installer_wizard/ui")
        else()
            add_custom_command(TARGET organizer POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E rm -rf "$<TARGET_FILE_DIR:organizer>/plugins/installer_wizard")
        endif()

        if(EXISTS "${MO2_UPSTREAM_PLUGIN_PAYLOAD_DIR}/pyCfg.py")
            add_custom_command(TARGET organizer POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${MO2_UPSTREAM_PLUGIN_PAYLOAD_DIR}/pyCfg.py"
                    "$<TARGET_FILE_DIR:organizer>/plugins/pyCfg.py")
        endif()

        if(EXISTS "${MO2_UPSTREAM_PLUGIN_PAYLOAD_DIR}/data")
            add_custom_command(TARGET organizer POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${MO2_UPSTREAM_PLUGIN_PAYLOAD_DIR}/data"
                    "$<TARGET_FILE_DIR:organizer>/plugins/data")
        endif()
    endif()

    # ── Deploy bundled stylesheets (themes) ──
    add_custom_command(TARGET organizer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/stylesheets"
            "$<TARGET_FILE_DIR:organizer>/stylesheets")

    # Keep only the canonical Fallout NV plugin filename on Linux (case-sensitive FS).
    add_custom_command(TARGET organizer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E rm -f "$<TARGET_FILE_DIR:organizer>/plugins/libgame_falloutnv.so")

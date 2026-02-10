cmake_minimum_required(VERSION 3.16)

find_package(Qt6 REQUIRED COMPONENTS
    Widgets WebEngineWidgets WebSockets Network)
find_package(Boost CONFIG REQUIRED COMPONENTS
    program_options)
find_package(spdlog CONFIG REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FUSE3 REQUIRED IMPORTED_TARGET fuse3)

qt_standard_project_setup()

# Collect all source files
file(GLOB ORGANIZER_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/shared/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/vfs/*.cpp)
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
    ${CMAKE_CURRENT_SOURCE_DIR}/fluorineconfig.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/protonlauncher.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/settingsdialogproton.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/wineprefix.cpp)
list(APPEND ORGANIZER_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/fluorineconfig.h
    ${CMAKE_CURRENT_SOURCE_DIR}/protonlauncher.h
    ${CMAKE_CURRENT_SOURCE_DIR}/settingsdialogproton.h
    ${CMAKE_CURRENT_SOURCE_DIR}/wineprefix.h)

# Remove Windows-only sources (replaced by Linux equivalents)
list(REMOVE_ITEM ORGANIZER_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/usvfsconnector.cpp)
list(REMOVE_ITEM ORGANIZER_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/usvfsconnector.h)
if(WIN32)
    list(REMOVE_ITEM ORGANIZER_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/nxmhandler_linux.cpp)
    list(REMOVE_ITEM ORGANIZER_HEADERS
        ${CMAKE_CURRENT_SOURCE_DIR}/nxmhandler_linux.h)
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

target_include_directories(organizer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
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
    mo2::lootcli-header
    # NaK FFI for game detection + Proton
    mo2::nak_ffi
    # Qt6
    Qt6::Widgets
    Qt6::WebEngineWidgets
    Qt6::WebSockets
    Qt6::Network
    # Boost
    Boost::program_options
    # spdlog
    spdlog::spdlog_header_only
    # FUSE3 (low-level API)
    PkgConfig::FUSE3
    # Linux system libs
    dl)

target_compile_definitions(organizer PRIVATE
    SPDLOG_USE_STD_FORMAT
    FUSE_USE_VERSION=35)

if(NOT WIN32)
    # Enforce PIE for the main executable to avoid copy relocations against Qt
    # symbols on newer glibc/toolchain combinations.
    include(CheckPIESupported)
    check_pie_supported()
    set_target_properties(organizer PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_compile_options(organizer PRIVATE -fPIE)
    target_link_options(organizer PRIVATE -pie -Wl,-z,nocopyreloc)
endif()

if(NOT WIN32)
    option(MO2_BUNDLE_7Z_RUNTIME "Copy a Linux 7z module into organizer/dlls" ON)
    option(MO2_STAGE_PYTHON_PLUGIN_PAYLOAD
        "Stage shipped Python plugin payload into build plugins/ for Linux runs" ON)
    option(MO2_BUNDLE_PYQT6_RUNTIME
        "Bundle PyQt6 into organizer/python/site-packages for portable Python plugins" ON)
    option(MO2_STAGE_INSTALLER_WIZARD
        "Stage installer_wizard python plugin payload (requires additional wizard package)" OFF)
    set(MO2_7Z_SO_PATH "" CACHE FILEPATH
        "Path to a compatible 7z shared module (exports CreateObject/GetHandlerProperty2)")
    set(MO2_UPSTREAM_PLUGIN_PAYLOAD_DIR
        "$ENV{HOME}/Downloads/Mod.Organizer-2.5.2/plugins"
        CACHE PATH "Optional path to upstream MO2 plugins folder for pyCfg.py/data payload")
    set(MO2_BUNDLED_PYTHON_SITEPACKAGES_SUBDIR
        "python/site-packages"
        CACHE STRING "Relative subdirectory under organizer for bundled Python site-packages")

    set(MO2_PYQT6_IMPORT_OK 1)
    set(MO2_PYTHON_PURELIB_DIR_RUNTIME "")
    if(Python_EXECUTABLE)
        execute_process(
            COMMAND "${Python_EXECUTABLE}" -c "import sysconfig; print(sysconfig.get_paths().get('purelib', ''))"
            OUTPUT_VARIABLE MO2_PYTHON_PURELIB_DIR_RUNTIME
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)
        execute_process(
            COMMAND "${Python_EXECUTABLE}" -c "import importlib.util; raise SystemExit(0 if importlib.util.find_spec('PyQt6.QtCore') else 1)"
            RESULT_VARIABLE MO2_PYQT6_IMPORT_OK
            OUTPUT_QUIET
            ERROR_QUIET)
    endif()

    if(MO2_BUNDLE_7Z_RUNTIME)
        if(TARGET 7z)
            # Use the 7z.so built from source by libs/7zip/CMakeLists.txt
            add_dependencies(organizer 7z)
            add_custom_command(TARGET organizer POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:organizer>/dlls"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "$<TARGET_FILE:7z>" "$<TARGET_FILE_DIR:organizer>/dlls/7z.so"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "$<TARGET_FILE:7z>" "$<TARGET_FILE_DIR:organizer>/dlls/7zip.dll")
            message(STATUS "[MO2] Will bundle 7z.so built from source")
        else()
            message(WARNING
                "[MO2] MO2_BUNDLE_7Z_RUNTIME is ON but the 7z target was not found. "
                "Ensure libs/7zip is included in the build.")
        endif()
    endif()

    if(MO2_STAGE_PYTHON_PLUGIN_PAYLOAD AND MO2_PYQT6_IMPORT_OK EQUAL 0)
        add_custom_command(TARGET organizer POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:organizer>/plugins/basic_games"
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:organizer>/plugins/data"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/libs/basic_games"
                "$<TARGET_FILE_DIR:organizer>/plugins/basic_games"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_SOURCE_DIR}/libs/preview_dds/src/DDSPreview.py"
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
    elseif(MO2_STAGE_PYTHON_PLUGIN_PAYLOAD)
        message(STATUS "[MO2] Skipping Python plugin payload: PyQt6.QtCore is not importable")
        add_custom_command(TARGET organizer POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E rm -f
                "$<TARGET_FILE_DIR:organizer>/plugins/pyCfg.py"
                "$<TARGET_FILE_DIR:organizer>/plugins/DDSPreview.py"
                "$<TARGET_FILE_DIR:organizer>/plugins/FNISPatches.py"
                "$<TARGET_FILE_DIR:organizer>/plugins/FNISTool.py"
                "$<TARGET_FILE_DIR:organizer>/plugins/FNISToolReset.py"
                "$<TARGET_FILE_DIR:organizer>/plugins/Form43Checker.py"
                "$<TARGET_FILE_DIR:organizer>/plugins/ScriptExtenderPluginChecker.py")
        add_custom_command(TARGET organizer POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E rm -rf
                "$<TARGET_FILE_DIR:organizer>/plugins/basic_games"
                "$<TARGET_FILE_DIR:organizer>/plugins/data")
    endif()

    if(MO2_BUNDLE_PYQT6_RUNTIME AND MO2_PYQT6_IMPORT_OK EQUAL 0 AND
       EXISTS "${MO2_PYTHON_PURELIB_DIR_RUNTIME}/PyQt6")
        add_custom_command(TARGET organizer POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:organizer>/${MO2_BUNDLED_PYTHON_SITEPACKAGES_SUBDIR}"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${MO2_PYTHON_PURELIB_DIR_RUNTIME}/PyQt6"
                "$<TARGET_FILE_DIR:organizer>/${MO2_BUNDLED_PYTHON_SITEPACKAGES_SUBDIR}/PyQt6")
        message(STATUS "[MO2] Will bundle PyQt6 runtime from ${MO2_PYTHON_PURELIB_DIR_RUNTIME}")
    endif()

    # ---------- Bundle umu-run (Python zipapp) ----------
    option(MO2_BUNDLE_UMU "Download and bundle umu-run launcher" ON)
    set(MO2_UMU_VERSION "1.3.0" CACHE STRING "umu-launcher release version to bundle")

    if(MO2_BUNDLE_UMU)
        set(UMU_TAR "${CMAKE_BINARY_DIR}/umu-launcher-${MO2_UMU_VERSION}-zipapp.tar")
        set(UMU_URL "https://github.com/Open-Wine-Components/umu-launcher/releases/download/${MO2_UMU_VERSION}/umu-launcher-${MO2_UMU_VERSION}-zipapp.tar")
        set(UMU_EXTRACT_DIR "${CMAKE_BINARY_DIR}/umu-extract")

        if(NOT EXISTS "${UMU_EXTRACT_DIR}/umu/umu-run")
            message(STATUS "[MO2] Downloading umu-launcher ${MO2_UMU_VERSION} zipapp...")
            file(DOWNLOAD "${UMU_URL}" "${UMU_TAR}"
                STATUS UMU_DL_STATUS
                SHOW_PROGRESS)
            list(GET UMU_DL_STATUS 0 UMU_DL_CODE)
            if(NOT UMU_DL_CODE EQUAL 0)
                message(WARNING "[MO2] Failed to download umu-launcher: ${UMU_DL_STATUS}")
            else()
                file(MAKE_DIRECTORY "${UMU_EXTRACT_DIR}")
                execute_process(
                    COMMAND ${CMAKE_COMMAND} -E tar xf "${UMU_TAR}"
                    WORKING_DIRECTORY "${UMU_EXTRACT_DIR}")
                message(STATUS "[MO2] Extracted umu-run zipapp")
            endif()
        endif()

        if(EXISTS "${UMU_EXTRACT_DIR}/umu/umu-run")
            add_custom_command(TARGET organizer POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${UMU_EXTRACT_DIR}/umu/umu-run"
                    "$<TARGET_FILE_DIR:organizer>/umu-run"
                COMMAND chmod +x "$<TARGET_FILE_DIR:organizer>/umu-run")
            message(STATUS "[MO2] Will bundle umu-run ${MO2_UMU_VERSION}")
        else()
            message(WARNING "[MO2] umu-run not found after extraction")
        endif()
    endif()

    # Keep only the canonical Fallout NV plugin filename on Linux (case-sensitive FS).
    add_custom_command(TARGET organizer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E rm -f "$<TARGET_FILE_DIR:organizer>/plugins/libgame_falloutnv.so")
endif()

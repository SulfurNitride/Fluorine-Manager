cmake_minimum_required(VERSION 3.16)
project(ModOrganizer2 VERSION 2.5.3 LANGUAGES C CXX)

if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()
if(POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Prevent COPY relocations against protected Qt6 symbols on modern distros
# (Fedora 41+, etc.) that set GNU_PROPERTY_1_NEEDED_INDIRECT_EXTERN_ACCESS.
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag(-mno-direct-extern-access HAS_NO_DIRECT_EXTERN_ACCESS)
if(HAS_NO_DIRECT_EXTERN_ACCESS)
    add_compile_options(-mno-direct-extern-access)
endif()

option(BUILD_TESTING "Build tests" OFF)
option(BUILD_NAK_FFI "Build NaK Rust FFI library" ON)
option(BUILD_BSA_FFI "Build Rust BSA/BA2 FFI library" ON)
option(BUILD_DOTNET_PLUGINS "Build C++/CLI .NET plugins (Windows+MSVC only)" OFF)
option(BUILD_PLUGIN_PYTHON "Build plugin_python bridge on Linux" ON)

# Compatibility helpers for upstream MO2 plugin repos that expect mo2-cmake.
if(NOT COMMAND mo2_configure_plugin)
  function(mo2_configure_plugin target)
    cmake_parse_arguments(MO2 "NO_SOURCES" "" "" ${ARGN})

    set_target_properties(${target} PROPERTIES
      AUTOMOC ON
      AUTOUIC ON
      AUTORCC ON
      CXX_STANDARD 23
      CXX_STANDARD_REQUIRED ON
      POSITION_INDEPENDENT_CODE ON
    )

    if(NOT MO2_NO_SOURCES)
      file(GLOB _mo2_plugin_sources CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/*.h
        ${CMAKE_CURRENT_SOURCE_DIR}/*.ui
        ${CMAKE_CURRENT_SOURCE_DIR}/*.qrc
      )
      if(_mo2_plugin_sources)
        target_sources(${target} PRIVATE ${_mo2_plugin_sources})
      endif()
    endif()

    target_compile_options(${target} PRIVATE
      "-idirafter" "${CMAKE_SOURCE_DIR}/libs/uibase/include/uibase"
    )
    set_property(TARGET ${target} APPEND PROPERTY AUTOMOC_MOC_OPTIONS
      "-I${CMAKE_SOURCE_DIR}/libs/uibase/include/uibase"
    )
  endfunction()
endif()

if(NOT COMMAND mo2_install_plugin)
  function(mo2_install_plugin target)
    install(TARGETS ${target}
      LIBRARY DESTINATION plugins
    )
  endfunction()
endif()

if(NOT COMMAND mo2_default_source_group)
  function(mo2_default_source_group)
    # no-op
  endfunction()
endif()

if(NOT COMMAND mo2_configure_target)
  function(mo2_configure_target target)
    set_target_properties(${target} PROPERTIES
      AUTOMOC ON
      AUTOUIC ON
      AUTORCC ON
      CXX_STANDARD 23
      CXX_STANDARD_REQUIRED ON
      POSITION_INDEPENDENT_CODE ON
    )
    target_compile_options(${target} PRIVATE
      "-idirafter" "${CMAKE_SOURCE_DIR}/libs/uibase/include/uibase"
    )
    set_property(TARGET ${target} APPEND PROPERTY AUTOMOC_MOC_OPTIONS
      "-I${CMAKE_SOURCE_DIR}/libs/uibase/include/uibase"
    )
  endfunction()
endif()

if(NOT COMMAND mo2_target_sources)
  function(mo2_target_sources target)
    set(_scope PRIVATE)
    set(_out)
    set(_skip_next OFF)
    foreach(_arg IN LISTS ARGN)
      if(_skip_next)
        set(_skip_next OFF)
        continue()
      endif()
      if(_arg STREQUAL "FOLDER")
        set(_skip_next ON)
        continue()
      endif()
      if(_arg STREQUAL "PRIVATE" OR _arg STREQUAL "PUBLIC" OR _arg STREQUAL "INTERFACE")
        set(_scope "${_arg}")
        continue()
      endif()
      list(APPEND _out "${_arg}")
    endforeach()
    if(_out)
      target_sources(${target} ${_scope} ${_out})
    endif()
  endfunction()
endif()

if(NOT COMMAND mo2_find_python_executable)
  function(mo2_find_python_executable out_var)
    if(DEFINED Python_EXECUTABLE AND Python_EXECUTABLE)
      set(${out_var} "${Python_EXECUTABLE}" PARENT_SCOPE)
      return()
    endif()
    find_program(_mo2_python_exe NAMES python3 python)
    set(${out_var} "${_mo2_python_exe}" PARENT_SCOPE)
  endfunction()
endif()

if(NOT COMMAND mo2_add_translations)
  function(mo2_add_translations)
    # no-op on Linux port for now
  endfunction()
endif()

if(NOT COMMAND mo2_python_install_pyqt)
  function(mo2_python_install_pyqt)
    # Use system/venv PyQt6 directly; no bundled wheel install step.
  endfunction()
endif()

# Make dds-header findable by bsatk and libbsarch
set(mo2-dds-header_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libs/dds-header")

# Sub-libraries in dependency order
add_subdirectory(libs/esptk)      # static lib, no deps
add_subdirectory(libs/bsatk)      # static lib, needs dds-header + boost + zlib + lz4
add_subdirectory(libs/libbsarch)  # shared + static, needs dds-header
add_subdirectory(libs/7zip)        # builds 7z.so from source (Linux only)
add_subdirectory(libs/archive)    # shared lib, needs dl
add_subdirectory(libs/lootcli)    # executable, needs Qt6 + boost + libloot + curl
add_subdirectory(libs/uibase)     # shared lib, needs Qt6 + spdlog

if(BUILD_NAK_FFI)
    add_subdirectory(libs/nak_ffi)
endif()

if(BUILD_BSA_FFI)
    add_subdirectory(libs/bsa_ffi)
endif()

if(BUILD_PLUGIN_PYTHON)
  set(_mo2_pyvenv_python "${CMAKE_SOURCE_DIR}/build/pyvenv/bin/python")
  if(EXISTS "${_mo2_pyvenv_python}")
    set(Python_EXECUTABLE "${_mo2_pyvenv_python}" CACHE FILEPATH
      "Python executable for plugin_python build" FORCE)
    set(pybind11_DIR
      "${CMAKE_SOURCE_DIR}/build/pyvenv/lib/python3.14/site-packages/pybind11/share/cmake/pybind11"
      CACHE PATH "pybind11 cmake dir for plugin_python" FORCE)
    set(MO2_PYLIBS_DIR "${CMAKE_SOURCE_DIR}/build/pyvenv"
      CACHE PATH "Python runtime root for plugin_python" FORCE)
  endif()
  add_subdirectory(libs/plugin_python)
endif()

# Game plugin chain: features (headers) → gamebryo (static) → creation (static) → game .so plugins
add_subdirectory(libs/game_features)
add_subdirectory(libs/game_bethesda)
add_subdirectory(libs/installer_fomod)
add_subdirectory(libs/installer_fomod_plus)
add_subdirectory(libs/installer_bain)
add_subdirectory(libs/installer_bundle)
add_subdirectory(libs/installer_manual)
add_subdirectory(libs/installer_quick)
add_subdirectory(libs/preview_base)
add_subdirectory(libs/preview_bsa)
add_subdirectory(libs/bsa_extractor)
add_subdirectory(libs/bsapacker)
add_subdirectory(libs/diagnose_basic)
add_subdirectory(libs/check_fnis)
add_subdirectory(libs/tool_inibakery)
add_subdirectory(libs/tool_inieditor)

if(BUILD_DOTNET_PLUGINS)
  if(WIN32 AND MSVC)
    add_subdirectory(libs/installer_fomod_csharp)
    add_subdirectory(libs/installer_omod)
  else()
    message(WARNING
      "BUILD_DOTNET_PLUGINS was enabled, but installer_fomod_csharp/installer_omod "
      "are C++/CLI targets that require Windows + MSVC. Skipping on this platform."
    )
  endif()
endif()

# Main organizer executable
add_subdirectory(src/src)

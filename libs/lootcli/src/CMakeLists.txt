cmake_minimum_required(VERSION 3.16)

# Qt is not required, this allows us to skip building the executable and only create
# the single-header interface when using this as a VCPKG dependency

find_package(Qt6 CONFIG COMPONENTS Core)

if (Qt6_FOUND)

message(STATUS "Qt6 found, building lootcli executable")

find_package(tomlplusplus CONFIG REQUIRED)
find_package(Boost REQUIRED CONFIG COMPONENTS locale)

# Try to find libloot via cmake config, then fall back to pkg-config
find_package(libloot CONFIG QUIET)
if(NOT libloot_FOUND)
  # On Linux with AUR package, libloot installs a .pc file
  find_package(PkgConfig)
  if(PkgConfig_FOUND)
    pkg_check_modules(LIBLOOT REQUIRED IMPORTED_TARGET libloot)
  endif()
endif()

if(TARGET libloot::loot)
  # avoid CMake error/warning
  set_target_properties(libloot::loot PROPERTIES
    MAP_IMPORTED_CONFIG_RELEASE RelWithDebInfo
    MAP_IMPORTED_CONFIG_MINSIZEREL RelWithDebInfo
  )
endif()

if(WIN32)
  add_executable(lootcli WIN32)
  set_target_properties(lootcli PROPERTIES
    CXX_STANDARD 20
    WIN32_EXECUTABLE TRUE)
else()
  add_executable(lootcli)
  set_target_properties(lootcli PROPERTIES
    CXX_STANDARD 20)
endif()

set(LOOTCLI_SOURCES
  game_settings.cpp
  game_settings.h
  lootthread.cpp
  lootthread.h
  main.cpp
  pch.h
  version.h
  ${CMAKE_CURRENT_SOURCE_DIR}/../include/lootcli/lootcli.h
)

if(WIN32)
  list(APPEND LOOTCLI_SOURCES version.rc)
endif()

target_sources(lootcli PRIVATE ${LOOTCLI_SOURCES})

if(WIN32)
  target_compile_definitions(lootcli
    PRIVATE
    _UNICODE UNICODE
    _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING)
endif()

target_include_directories(lootcli PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_precompile_headers(lootcli PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/pch.h)

# Link libraries
if(TARGET libloot::loot)
  target_link_libraries(lootcli PRIVATE libloot::loot)
elseif(TARGET PkgConfig::LIBLOOT)
  target_link_libraries(lootcli PRIVATE PkgConfig::LIBLOOT)
else()
  # Fallback: try to find the library manually
  find_library(LIBLOOT_LIBRARY NAMES loot libloot)
  find_path(LIBLOOT_INCLUDE_DIR NAMES loot/api.h)
  if(LIBLOOT_LIBRARY AND LIBLOOT_INCLUDE_DIR)
    target_link_libraries(lootcli PRIVATE ${LIBLOOT_LIBRARY})
    target_include_directories(lootcli PRIVATE ${LIBLOOT_INCLUDE_DIR})
  else()
    message(FATAL_ERROR "libloot not found. Install libloot or set CMAKE_PREFIX_PATH.")
  endif()
endif()

target_link_libraries(lootcli
    PRIVATE Boost::headers Boost::locale
    tomlplusplus::tomlplusplus Qt6::Core)

if(NOT WIN32)
  find_package(CURL REQUIRED)
  target_link_libraries(lootcli PRIVATE CURL::libcurl)
endif()

if (MSVC)
  target_compile_options(lootcli
    PRIVATE
    "/MP"
    "/W4"
    "/external:anglebrackets"
    "/external:W0"
  )
  target_link_options(lootcli
    PRIVATE
    $<$<CONFIG:RelWithDebInfo>:/LTCG /INCREMENTAL:NO /OPT:REF /OPT:ICF>
  )
  target_compile_definitions(lootcli PRIVATE _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING)

  set_target_properties(lootcli PROPERTIES VS_STARTUP_PROJECT lootcli)
endif()

if(WIN32)
  install(FILES
    $<TARGET_FILE:lootcli>
    $<TARGET_FILE:libloot::loot>
  DESTINATION bin/loot)
else()
  install(TARGETS lootcli DESTINATION bin/loot)
endif()

endif()

# library to make the header available
add_library(lootcli-header INTERFACE)
target_sources(lootcli-header INTERFACE
  FILE_SET HEADERS
  BASE_DIRS ${CMAKE_CURRENT_LIST_DIR}/../include
  FILES ${CMAKE_CURRENT_LIST_DIR}/../include/lootcli/lootcli.h)
add_library(mo2::lootcli-header ALIAS lootcli-header)

install(TARGETS lootcli-header EXPORT lootcliHeaderTargets FILE_SET HEADERS)
install(EXPORT lootcliHeaderTargets
  FILE mo2-lootcli-header-targets.cmake
  NAMESPACE mo2::
  DESTINATION lib/cmake/mo2-lootcli-header
)

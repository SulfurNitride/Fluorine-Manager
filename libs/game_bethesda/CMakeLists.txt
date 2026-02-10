cmake_minimum_required(VERSION 3.16)
project(game_bethesda)

find_package(Qt6 COMPONENTS Widgets REQUIRED)

# Compatibility helpers for upstream plugin CMake files that still call mo2-cmake
# functions. We implement only what this Linux port needs.
function(mo2_configure_plugin target)
  set_target_properties(${target} PROPERTIES
    AUTOMOC ON
    AUTOUIC ON
    AUTORCC ON
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
  )

  # Keep bare uibase headers after system headers to avoid strings.h collisions.
  target_compile_options(${target} PRIVATE
    "-idirafter" "${CMAKE_SOURCE_DIR}/libs/uibase/include/uibase"
  )
  set_property(TARGET ${target} APPEND PROPERTY AUTOMOC_MOC_OPTIONS
    "-I${CMAKE_SOURCE_DIR}/libs/uibase/include/uibase"
  )
endfunction()

function(mo2_default_source_group)
  # no-op for now
endfunction()

function(mo2_install_plugin target)
  install(TARGETS ${target}
    LIBRARY DESTINATION plugins
  )
endfunction()

add_subdirectory(src/gamebryo)
add_subdirectory(src/creation)

# Build all ported game plugins.
add_subdirectory(src/games/skyrimse)
add_subdirectory(src/games/skyrim)
add_subdirectory(src/games/skyrimvr)
add_subdirectory(src/games/fallout4)
add_subdirectory(src/games/fallout4vr)
add_subdirectory(src/games/fallout3)
add_subdirectory(src/games/falloutnv)
add_subdirectory(src/games/fallout76)
add_subdirectory(src/games/oblivion)
add_subdirectory(src/games/morrowind)
add_subdirectory(src/games/nehrim)
add_subdirectory(src/games/enderal)
add_subdirectory(src/games/enderalse)
add_subdirectory(src/games/starfield)
add_subdirectory(src/games/ttw)
add_subdirectory(src/games/fallout4london)

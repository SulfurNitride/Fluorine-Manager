cmake_minimum_required(VERSION 3.16)
project(game_features)

# Header-only interface library
# The canonical game feature headers live inside uibase's game_features/ subdirectory.
# We expose them here so game plugins can #include <bsainvalidation.h>, <igamefeatures.h>, etc.
add_library(game_features INTERFACE)
target_include_directories(game_features INTERFACE
    ${CMAKE_SOURCE_DIR}/libs/uibase/include/uibase/game_features
)

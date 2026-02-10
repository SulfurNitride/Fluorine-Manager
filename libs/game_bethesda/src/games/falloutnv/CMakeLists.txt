cmake_minimum_required(VERSION 3.16)

add_library(game_falloutnv SHARED
	falloutnvbsainvalidation.cpp
	falloutnvbsainvalidation.h
	falloutnvdataarchives.cpp
	falloutnvdataarchives.h
	falloutnvmoddatachecker.h
	falloutnvmoddatacontent.h
	falloutnvsavegame.cpp
	falloutnvsavegame.h
	falloutnvscriptextender.cpp
	falloutnvscriptextender.h
	gamefalloutnv.cpp
	gamefalloutnv.h
)
mo2_configure_plugin(game_falloutnv NO_SOURCES WARNINGS 4)
mo2_default_source_group()
target_link_libraries(game_falloutnv PRIVATE game_gamebryo)
set_target_properties(game_falloutnv PROPERTIES OUTPUT_NAME "game_falloutNV")
mo2_install_plugin(game_falloutnv)

if(NOT WIN32)
  add_custom_command(TARGET game_falloutnv POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/src/src/plugins"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "$<TARGET_FILE:game_falloutnv>"
      "${CMAKE_BINARY_DIR}/src/src/plugins/libgame_falloutNV.so"
    COMMAND ${CMAKE_COMMAND} -E rm -f
      "${CMAKE_BINARY_DIR}/src/src/plugins/libgame_falloutnv.so")
endif()

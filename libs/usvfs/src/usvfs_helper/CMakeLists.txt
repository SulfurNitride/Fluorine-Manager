cmake_minimum_required(VERSION 3.16)

add_library(usvfs_helper STATIC inject.h inject.cpp)
target_link_libraries(usvfs_helper PUBLIC shared PRIVATE tinjectlib)
target_include_directories(usvfs_helper PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(usvfs_helper PROPERTIES FOLDER injection)

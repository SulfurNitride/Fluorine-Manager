if (TARGET mo2::dds-header)
  return()
endif()

add_library(mo2::dds-header INTERFACE IMPORTED)
target_include_directories(mo2::dds-header INTERFACE ${CMAKE_CURRENT_LIST_DIR}/include/DDS)

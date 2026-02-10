if (TARGET mo2::dds-header)
  return()
endif()

get_filename_component(_IMPORT_PREFIX "${CMAKE_CURRENT_LIST_FILE}" PATH)
get_filename_component(_IMPORT_PREFIX "${_IMPORT_PREFIX}" PATH)
get_filename_component(_IMPORT_PREFIX "${_IMPORT_PREFIX}" PATH)

add_library(mo2::dds-header INTERFACE IMPORTED)
target_include_directories(mo2::dds-header INTERFACE ${_IMPORT_PREFIX}/include/DDS)

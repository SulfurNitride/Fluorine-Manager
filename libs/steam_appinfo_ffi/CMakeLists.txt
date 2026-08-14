cmake_minimum_required(VERSION 3.16)
project(steam_appinfo_ffi)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(CARGO_BUILD_TYPE "debug")
  set(CARGO_BUILD_FLAGS "")
else()
  set(CARGO_BUILD_TYPE "release")
  set(CARGO_BUILD_FLAGS "--release")
endif()

set(SAI_FFI_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(SAI_FFI_LIB ${SAI_FFI_DIR}/target/${CARGO_BUILD_TYPE}/libsteam_appinfo_ffi.so)

add_custom_target(steam_appinfo_ffi_build
  COMMAND cargo build ${CARGO_BUILD_FLAGS}
  WORKING_DIRECTORY ${SAI_FFI_DIR}
  COMMENT "Building Steam appinfo FFI library (Rust)"
  BYPRODUCTS ${SAI_FFI_LIB}
  VERBATIM
)

add_library(steam_appinfo_ffi_wrapper INTERFACE)
target_link_libraries(steam_appinfo_ffi_wrapper INTERFACE ${SAI_FFI_LIB})
target_include_directories(steam_appinfo_ffi_wrapper INTERFACE ${SAI_FFI_DIR}/include)
add_dependencies(steam_appinfo_ffi_wrapper steam_appinfo_ffi_build)
add_library(mo2::steam_appinfo_ffi ALIAS steam_appinfo_ffi_wrapper)

install(FILES ${SAI_FFI_LIB} DESTINATION lib)
install(DIRECTORY ${SAI_FFI_DIR}/include/ DESTINATION include)

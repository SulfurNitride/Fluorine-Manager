cmake_minimum_required(VERSION 3.16)
project(nak_ffi)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CARGO_BUILD_TYPE "debug")
    set(CARGO_BUILD_FLAGS "")
else()
    set(CARGO_BUILD_TYPE "release")
    set(CARGO_BUILD_FLAGS "--release")
endif()

set(NAK_FFI_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(NAK_FFI_LIB ${NAK_FFI_DIR}/target/${CARGO_BUILD_TYPE}/libnak_ffi.so)

add_custom_command(
    OUTPUT ${NAK_FFI_LIB}
    COMMAND cargo build ${CARGO_BUILD_FLAGS}
    WORKING_DIRECTORY ${NAK_FFI_DIR}
    COMMENT "Building NaK FFI library (Rust)"
    DEPENDS ${NAK_FFI_DIR}/Cargo.toml ${NAK_FFI_DIR}/src/lib.rs
)

add_custom_target(nak_ffi_build DEPENDS ${NAK_FFI_LIB})

# INTERFACE wrapper so ALIAS works (CMake can't alias IMPORTED targets)
add_library(nak_ffi_wrapper INTERFACE)
target_link_libraries(nak_ffi_wrapper INTERFACE ${NAK_FFI_LIB})
target_include_directories(nak_ffi_wrapper INTERFACE ${NAK_FFI_DIR}/include)
add_dependencies(nak_ffi_wrapper nak_ffi_build)
add_library(mo2::nak_ffi ALIAS nak_ffi_wrapper)

install(FILES ${NAK_FFI_LIB} DESTINATION lib)
install(DIRECTORY ${NAK_FFI_DIR}/include/ DESTINATION include)

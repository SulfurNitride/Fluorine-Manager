cmake_minimum_required(VERSION 3.16)

find_package(Qt6 REQUIRED COMPONENTS Concurrent)
find_path(BEXT_DI_INCLUDE_DIRS "boost/di.hpp")

file(GLOB bsapacker_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/*.h
  ${CMAKE_CURRENT_SOURCE_DIR}/*.ui
  ${CMAKE_CURRENT_SOURCE_DIR}/*.qrc
  ${CMAKE_CURRENT_SOURCE_DIR}/bsapacker/*.h
  ${CMAKE_CURRENT_SOURCE_DIR}/qlibbsarch/*.h
)

add_library(bsa_packer SHARED ${bsapacker_SOURCES})
mo2_configure_plugin(bsa_packer NO_SOURCES WARNINGS OFF)

target_link_libraries(bsa_packer PUBLIC mo2::uibase Qt6::Concurrent libbsarch_OOP)
target_include_directories(bsa_packer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(BEXT_DI_INCLUDE_DIRS)
  target_include_directories(bsa_packer PUBLIC ${BEXT_DI_INCLUDE_DIRS})
endif()
target_compile_definitions(bsa_packer PRIVATE BSAPACKER_LIBRARY)

if(TARGET mo2::bsa_ffi)
  target_link_libraries(bsa_packer PUBLIC mo2::bsa_ffi)
endif()

# need to deploy this here as MO2 does not depends on Qt6::Concurrent
install(FILES $<TARGET_FILE:Qt6::Concurrent> DESTINATION bin/dlls)

# this is done by modorganizer itself now
#
# install(FILES $<TARGET_FILE:mo2::libbsarch> DESTINATION bin/dlls)

mo2_install_plugin(bsa_packer)

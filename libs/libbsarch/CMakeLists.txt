cmake_minimum_required(VERSION 3.16)

include(CMakePackageConfigHelpers)

project(libbsarch CXX)

find_package(mo2-dds-header CONFIG REQUIRED)

get_target_property(SRCS mo2::dds-header SOURCES)

# libbsarch
add_library(libbsarch SHARED)
if(WIN32)
    target_sources(libbsarch
        PRIVATE
            src/libbsarch.cpp
            src/libbsarch.def
        PUBLIC
            FILE_SET HEADERS
            BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include
            FILES ${CMAKE_CURRENT_SOURCE_DIR}/include/libbsarch/libbsarch.h
    )
else()
    target_sources(libbsarch
        PRIVATE
            src/libbsarch.cpp
        PUBLIC
            FILE_SET HEADERS
            BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include
            FILES ${CMAKE_CURRENT_SOURCE_DIR}/include/libbsarch/libbsarch.h
    )
    # Set default symbol visibility to hidden, export only marked symbols
    target_compile_options(libbsarch PRIVATE -fvisibility=hidden)
endif()
target_include_directories(libbsarch PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include/libbsarch)
set_target_properties(libbsarch PROPERTIES DEBUG_POSTFIX "d" CXX_STANDARD 11)
target_link_libraries(libbsarch PUBLIC mo2::dds-header)
target_compile_definitions(libbsarch PRIVATE BSARCH_DLL_EXPORT)

# preprocessor definitions
if(MSVC)
    target_compile_options(libbsarch PRIVATE
        /W3 /EHsc /MP
        $<$<CONFIG:DEBUG>:/MDd /Od>
        $<$<CONFIG:RELEASE>:/Zi>
    )
endif()


# libbsarch_OOP
add_library(libbsarch_OOP STATIC)

target_sources(libbsarch_OOP
    PRIVATE
        src/bs_archive_entries.cpp
        src/utils/string_convert.cpp
        src/utils/convertible_ostream.cpp
        src/utils/convertible_string.cpp
        src/bs_archive_auto.cpp
        src/bs_archive.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include
        FILES
            ${CMAKE_CURRENT_SOURCE_DIR}/include/libbsarch/base_types.hpp
            ${CMAKE_CURRENT_SOURCE_DIR}/include/libbsarch/bs_archive_auto.hpp
            ${CMAKE_CURRENT_SOURCE_DIR}/include/libbsarch/bs_archive.h
            ${CMAKE_CURRENT_SOURCE_DIR}/include/libbsarch/bs_archive_entries.h
            ${CMAKE_CURRENT_SOURCE_DIR}/include/libbsarch/libbsarch.hpp
            ${CMAKE_CURRENT_SOURCE_DIR}/include/libbsarch/libbsarch.h
            ${CMAKE_CURRENT_SOURCE_DIR}/include/libbsarch/utils/convertible_string.hpp
            ${CMAKE_CURRENT_SOURCE_DIR}/include/libbsarch/utils/convertible_ostream.hpp
            ${CMAKE_CURRENT_SOURCE_DIR}/include/libbsarch/utils/string_convert.hpp
)
target_include_directories(libbsarch_OOP PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include/libbsarch
    ${CMAKE_CURRENT_SOURCE_DIR}/include/libbsarch/utils)
target_link_libraries(libbsarch_OOP PUBLIC mo2::dds-header)

set_target_properties(libbsarch_OOP PROPERTIES
    DEBUG_POSTFIX "d"
    CXX_STANDARD 17
)

if (MSVC)
    target_compile_options(libbsarch_OOP PRIVATE /MP)
endif()

# target_link_libraries(libbsarch_OOP PUBLIC libbsarch)

# install

# finding 32 or 64bits
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    # 64 bits
    set(lib_delphi "${CMAKE_CURRENT_SOURCE_DIR}/delphi/lib64")
    message("libbsarch: using 64bits DLL")
elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
    # 32 bits
    set(lib_delphi "${CMAKE_CURRENT_SOURCE_DIR}/delphi/lib")
    message("libbsarch: using 32bits DLL")
endif()

install(TARGETS libbsarch libbsarch_OOP EXPORT libbsarchTargets FILE_SET HEADERS)
if(WIN32)
    install(FILES $<TARGET_PDB_FILE:libbsarch> DESTINATION pdb OPTIONAL)
endif()
install(EXPORT libbsarchTargets
        FILE mo2-libbsarch-targets.cmake
        NAMESPACE mo2::
        DESTINATION lib/cmake/mo2-libbsarch
)

if(WIN32)
    # install the true DLLs (Windows only - Delphi-built BSArch)
    install(FILES ${lib_delphi}/libbsarch.dll DESTINATION bin CONFIGURATIONS Release RelWithDebInfo MinSizeRel)
    install(FILES ${lib_delphi}/libbsarchd.dll DESTINATION bin CONFIGURATIONS Debug)
endif()

configure_package_config_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake/config.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/mo2-libbsarch-config.cmake"
  INSTALL_DESTINATION "lib/cmake/mo2-libbsarch"
  NO_SET_AND_CHECK_MACRO
  NO_CHECK_REQUIRED_COMPONENTS_MACRO
)


include(${CMAKE_CURRENT_SOURCE_DIR}/Version.cmake)
set(LIBBSARCH_VERSION
  ${LIBBSARCH_MAJOR_VERSION}.${LIBBSARCH_MINOR_VERSION}.${LIBBSARCH_PATCH_VERSION})


write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/mo2-libbsarch-config-version.cmake"
  VERSION "${LIBBSARCH_VERSION}"
  COMPATIBILITY AnyNewerVersion
  ARCH_INDEPENDENT
)

install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/mo2-libbsarch-config.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/mo2-libbsarch-config-version.cmake
  DESTINATION lib/cmake/mo2-libbsarch
)

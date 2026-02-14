cmake_minimum_required(VERSION 3.16)
if(POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)
endif()

find_package(Boost REQUIRED COMPONENTS thread)
find_package(ZLIB REQUIRED)
find_package(mo2-dds-header CONFIG REQUIRED)

# lz4: try CONFIG first, fall back to pkg-config
find_package(lz4 CONFIG QUIET)
if (NOT lz4_FOUND)
	find_package(PkgConfig REQUIRED)
	pkg_check_modules(LZ4 REQUIRED IMPORTED_TARGET liblz4)
endif()
# lz4 1.10+ exports LZ4::lz4, older versions export lz4::lz4
if(TARGET LZ4::lz4 AND NOT TARGET lz4::lz4)
	add_library(lz4::lz4 ALIAS LZ4::lz4)
endif()

add_library(bsatk STATIC)
target_link_libraries(bsatk
	PUBLIC mo2::dds-header
	PRIVATE ZLIB::ZLIB Boost::thread
)

# Link lz4 based on how it was found
if (lz4_FOUND)
	target_link_libraries(bsatk PRIVATE lz4::lz4)
else()
	target_link_libraries(bsatk PRIVATE PkgConfig::LZ4)
endif()

target_sources(bsatk
	PRIVATE
		bsaarchive.cpp
		bsaexception.cpp
		bsafile.cpp
		bsafolder.cpp
		bsatypes.cpp
		filehash.cpp
	PUBLIC
		FILE_SET HEADERS
		BASE_DIRS ${CMAKE_CURRENT_LIST_DIR}/../include
		FILES
		${CMAKE_CURRENT_LIST_DIR}/../include/bsatk/bsaarchive.h
		${CMAKE_CURRENT_LIST_DIR}/../include/bsatk/bsaexception.h
		${CMAKE_CURRENT_LIST_DIR}/../include/bsatk/bsafile.h
		${CMAKE_CURRENT_LIST_DIR}/../include/bsatk/bsafolder.h
		${CMAKE_CURRENT_LIST_DIR}/../include/bsatk/bsatk.h
		${CMAKE_CURRENT_LIST_DIR}/../include/bsatk/bsatypes.h
		${CMAKE_CURRENT_LIST_DIR}/../include/bsatk/errorcodes.h
		${CMAKE_CURRENT_LIST_DIR}/../include/bsatk/filehash.h

)

set_target_properties(bsatk PROPERTIES CXX_STANDARD 20)

# for building
target_include_directories(bsatk PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../include/bsatk)

if (MSVC)
	target_compile_options(bsatk
		PRIVATE
		"/MP"
		"/W4"
		"/external:anglebrackets"
		"/external:W0"
	)
	target_link_options(bsatk
		PRIVATE
		$<$<CONFIG:RelWithDebInfo>:/LTCG /INCREMENTAL:NO /OPT:REF /OPT:ICF>
	)

	set_target_properties(bsatk PROPERTIES VS_STARTUP_PROJECT bsatk)
endif()

add_library(mo2::bsatk ALIAS bsatk)

# install
install(TARGETS bsatk EXPORT bsatkTargets FILE_SET HEADERS)
install(EXPORT bsatkTargets
	FILE mo2-bsatk-targets.cmake
	NAMESPACE mo2::
	DESTINATION lib/cmake/mo2-bsatk
)

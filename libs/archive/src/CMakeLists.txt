cmake_minimum_required(VERSION 3.16)

if(WIN32)
	find_package(7zip CONFIG REQUIRED)
endif()

add_library(archive)

set_target_properties(archive PROPERTIES CXX_STANDARD 20)

# On Linux, we use the bundled 7zip SDK headers and dlopen the 7z.so at runtime
# On Windows, we use the vcpkg 7zip package
if(WIN32)
	target_link_libraries(archive PRIVATE 7zip::7zip)
else()
	# Add 7zip SDK headers from thirdparty
	target_include_directories(archive PRIVATE
		${CMAKE_CURRENT_LIST_DIR}/../thirdparty/CPP
	)
	# We need to compile MyWindows.cpp for BSTR/Variant support on Linux
	target_sources(archive PRIVATE
		${CMAKE_CURRENT_LIST_DIR}/../thirdparty/CPP/Common/MyWindows.cpp
	)
	target_link_libraries(archive PRIVATE dl)
	target_compile_definitions(archive PRIVATE -DARCHIVE_LINUX_PORT)
endif()

set(ARCHIVE_SOURCES
	archive.cpp
	compat.h
	extractcallback.cpp
	extractcallback.h
	fileio.cpp
	fileio.h
	formatter.h
	inputstream.cpp
	inputstream.h
	instrument.h
	interfaceguids.cpp
	library.h
	multioutputstream.cpp
	multioutputstream.h
	opencallback.cpp
	opencallback.h
	propertyvariant.cpp
	propertyvariant.h
	unknown_impl.h
)

if(WIN32)
	list(APPEND ARCHIVE_SOURCES version.rc)
endif()

target_sources(archive
	PRIVATE
		${ARCHIVE_SOURCES}
	PUBLIC
		FILE_SET HEADERS
		BASE_DIRS ${CMAKE_CURRENT_LIST_DIR}/../include
		FILES ${CMAKE_CURRENT_LIST_DIR}/../include/archive/archive.h
)

target_include_directories(archive PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../include/archive)

if (MSVC)
	target_compile_options(archive
		PRIVATE
		"/MP"
		"/W4"
		"/external:anglebrackets"
		"/external:W0"
	)
	target_link_options(archive
		PRIVATE
		$<$<CONFIG:RelWithDebInfo>:/LTCG /INCREMENTAL:NO /OPT:REF /OPT:ICF>
	)

	set_target_properties(archive PROPERTIES VS_STARTUP_PROJECT archive)
endif()

if(NOT MSVC)
	target_compile_options(archive PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

if (BUILD_STATIC)
	target_compile_definitions(archive PUBLIC -DMO2_ARCHIVE_BUILD_STATIC)
else()
	target_compile_definitions(archive PRIVATE -DMO2_ARCHIVE_BUILD_EXPORT)
endif()

add_library(mo2::archive ALIAS archive)

install(TARGETS archive EXPORT archiveTargets FILE_SET HEADERS)
if (NOT BUILD_STATIC AND WIN32)
	install(FILES $<TARGET_PDB_FILE:archive> DESTINATION pdb OPTIONAL)
endif()
install(EXPORT archiveTargets
	FILE mo2-archive-targets.cmake
	NAMESPACE mo2::
	DESTINATION lib/cmake/mo2-archive
)

cmake_minimum_required(VERSION 3.16)

add_library(esptk STATIC)

target_sources(esptk
	PRIVATE
        espfile.cpp
        record.cpp
        subrecord.cpp
        tes3record.cpp
        tes3subrecord.cpp
	PUBLIC
		FILE_SET HEADERS
		BASE_DIRS ${CMAKE_CURRENT_LIST_DIR}/../include
		FILES
        ${CMAKE_CURRENT_LIST_DIR}/../include/esptk/espexceptions.h
        ${CMAKE_CURRENT_LIST_DIR}/../include/esptk/espfile.h
        ${CMAKE_CURRENT_LIST_DIR}/../include/esptk/esptypes.h
        ${CMAKE_CURRENT_LIST_DIR}/../include/esptk/record.h
        ${CMAKE_CURRENT_LIST_DIR}/../include/esptk/subrecord.h
        ${CMAKE_CURRENT_LIST_DIR}/../include/esptk/tes3record.h
        ${CMAKE_CURRENT_LIST_DIR}/../include/esptk/tes3subrecord.h
)


set_target_properties(esptk PROPERTIES CXX_STANDARD 20)

# for building
target_include_directories(esptk PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../include/esptk)

if (MSVC)
	target_compile_options(esptk
		PRIVATE
		"/MP"
		"/Wall"
		"/external:anglebrackets"
		"/external:W0"
	)
	target_link_options(esptk
		PRIVATE
		$<$<CONFIG:RelWithDebInfo>:/LTCG /INCREMENTAL:NO /OPT:REF /OPT:ICF>
	)

	set_target_properties(esptk PROPERTIES VS_STARTUP_PROJECT esptk)
endif()

add_library(mo2::esptk ALIAS esptk)

# isntall
install(TARGETS esptk EXPORT esptkTargets FILE_SET HEADERS)
install(EXPORT esptkTargets
	FILE mo2-esptk-targets.cmake
	NAMESPACE mo2::
	DESTINATION lib/cmake/mo2-esptk
)

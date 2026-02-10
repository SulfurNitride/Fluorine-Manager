cmake_minimum_required(VERSION 3.16)

include(CMakeParseArguments)

#! usvfs_set_test_properties
#
# this function sets the following properties on the given executable or shared
# library test target:
# - OUTPUT_NAME to add arch-specific prefix
# - OUTPUT_DIRECTORY to put the test executable or shared library in the right location
# - FOLDER to organize the VS solution layout
#
# \param:FOLDER if present, specifies the subfolder to use in the solution
#
function(usvfs_set_test_properties TARGET)
	cmake_parse_arguments(USVFS_TEST "" "FOLDER" "" ${ARGN})
    if (NOT DEFINED USVFS_TEST_FOLDER)
		set(folder "tests")
    else()
        set(folder "tests/${USVFS_TEST_FOLDER}")
	endif()
    set_target_properties(${TARGET}
        PROPERTIES
        FOLDER ${folder}
        RUNTIME_OUTPUT_NAME ${TARGET}${ARCH_POSTFIX}
        RUNTIME_OUTPUT_DIRECTORY_DEBUG ${USVFS_TEST_BINDIR}
        RUNTIME_OUTPUT_DIRECTORY_RELEASE ${USVFS_TEST_BINDIR}
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${USVFS_TEST_BINDIR}
    )
endfunction()

#! usvfs_target_link_usvfs
#
# add a target link between the given target and the usvfs shared library with delay
# loading
#
function(usvfs_target_link_usvfs TARGET)
    target_link_libraries(${TARGET} PRIVATE usvfs_dll)
    target_link_options(${TARGET} PRIVATE "/DELAYLOAD:usvfs${ARCH_POSTFIX}.dll")
endfunction()


file(GLOB directories LIST_DIRECTORIES true "*")

# this goes through all the directories and
#
# 1. add them if there is a CMakeLists.txt inside
# 2. add correspondings tests (for CTest) for BOTH x86 and x64 when possible
#
foreach(directory ${directories})
    if(NOT(IS_DIRECTORY ${directory}))
        continue()
    endif()

    if(NOT(EXISTS ${directory}/CMakeLists.txt))
        continue()
    endif()

    add_subdirectory(${directory})

    get_filename_component(dirname ${directory} NAME)
    if((dirname STREQUAL "test_utils") OR (dirname STREQUAL "gtest_utils"))
        continue()
    endif()

    add_test(NAME ${dirname}_x64
        COMMAND ${USVFS_TEST_BINDIR}/${dirname}_x64.exe
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    )
    add_test(NAME ${dirname}_x86
        COMMAND ${USVFS_TEST_BINDIR}/${dirname}_x86.exe
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    )
endforeach()

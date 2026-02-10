cmake_minimum_required(VERSION 3.22)

if (DEFINED MO2_VERSIONS_INCLUDED)
	return()
endif()

# define MO2_QT_VERSION and related variables
# - if MO2_QT_VERSION is already defined, simply extract the major, minor and patch
#   components for
# - otherwise, if the project is a C++ project, look-up Qt and use the version from
#   the package found
# - otherwise, or if Qt was not found in the previous step, use a default version
#
if (NOT DEFINED MO2_QT_VERSION)
	if (CMAKE_CXX_COMPILER_LOADED)
		find_package(Qt6 CONFIG QUIET)
	endif()

	if (Qt6_FOUND)
		set(MO2_QT_VERSION "${Qt6_VERSION}")
	else()
		set(MO2_QT_VERSION "6.7.3")
	endif()
endif()

string(REPLACE "." ";" MO2_QT_VERSION_LIST ${MO2_QT_VERSION})
list(GET MO2_QT_VERSION_LIST 0 MO2_QT_VERSION_MAJOR)
list(GET MO2_QT_VERSION_LIST 1 MO2_QT_VERSION_MINOR)
list(GET MO2_QT_VERSION_LIST 2 MO2_QT_VERSION_PATCH)
unset(MO2_QT_VERSION_LIST)

message(STATUS "[MO2] Qt version: ${MO2_QT_VERSION} (${MO2_QT_VERSION_MAJOR}, ${MO2_QT_VERSION_MINOR}, ${MO2_QT_VERSION_PATCH})")

mo2_set_if_not_defined(MO2_PYTHON_VERSION "3.12")

if (MO2_QT_VERSION_MAJOR EQUAL 6 AND MO2_QT_VERSION_MINOR EQUAL 10)
    mo2_set_if_not_defined(MO2_PYQT_VERSION "6.10.2")
    mo2_set_if_not_defined(MO2_SIP_VERSION "6.15.1")
else()
    mo2_set_if_not_defined(MO2_PYQT_VERSION "6.7.1")
    mo2_set_if_not_defined(MO2_SIP_VERSION "6.8.6")
endif()

message(STATUS "[MO2] Python version: ${MO2_PYTHON_VERSION}")
message(STATUS "[MO2] PyQt version: ${MO2_PYQT_VERSION}")
message(STATUS "[MO2] SIP version: ${MO2_SIP_VERSION}")

# mark as included
set(MO2_VERSIONS_INCLUDED TRUE)

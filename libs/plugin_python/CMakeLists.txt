cmake_minimum_required(VERSION 3.16)

cmake_policy(SET CMP0144 NEW)

project(plugin_python CXX)
set(MO2_QT_VERSION_MAJOR 6)

find_package(Python COMPONENTS Interpreter Development REQUIRED)
find_package(pybind11 CONFIG REQUIRED)

get_filename_component(Python_HOME ${Python_EXECUTABLE} PATH)
set(Python_DLL_DIR "${Python_HOME}")
execute_process(
  COMMAND ${Python_EXECUTABLE} -c "import sysconfig; print(sysconfig.get_path('stdlib'))"
  OUTPUT_VARIABLE Python_LIB_DIR
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
execute_process(
  COMMAND ${Python_EXECUTABLE} -c "import sysconfig; print(sysconfig.get_path('purelib'))"
  OUTPUT_VARIABLE Python_PURELIB_DIR
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
execute_process(
  COMMAND ${Python_EXECUTABLE} -c "import sysconfig; print(sysconfig.get_path('platlib'))"
  OUTPUT_VARIABLE Python_PLATLIB_DIR
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

set(Python_SHARED_LIBRARY "")
if(DEFINED Python_LIBRARIES AND Python_LIBRARIES)
  list(GET Python_LIBRARIES 0 Python_SHARED_LIBRARY)
elseif(DEFINED Python_LIBRARY_RELEASE AND Python_LIBRARY_RELEASE)
  set(Python_SHARED_LIBRARY "${Python_LIBRARY_RELEASE}")
elseif(DEFINED Python_LIBRARY AND Python_LIBRARY)
  set(Python_SHARED_LIBRARY "${Python_LIBRARY}")
endif()

mo2_python_install_pyqt()

# useful for naming DLL, zip, etc. (3.10 -> 310)
set(Python_VERSION_SHORT ${Python_VERSION_MAJOR}${Python_VERSION_MINOR})

# projects
add_subdirectory(src)

# tests (if requested)
set(BUILD_TESTING ${BUILD_TESTING} CACHE BOOL "build tests for plugin_python")
if (BUILD_TESTING)
	enable_testing()
	add_subdirectory(tests)
endif()

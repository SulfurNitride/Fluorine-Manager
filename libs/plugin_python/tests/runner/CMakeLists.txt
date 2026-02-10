cmake_minimum_required(VERSION 3.22)

# setting-up the tests for the runner is a bit complex because we need a tons of
# things

# first we configure the tests as with other tests
add_executable(runner-tests EXCLUDE_FROM_ALL)
mo2_default_source_group()
mo2_target_sources(runner-tests
	FOLDER src
	PRIVATE
		test_diagnose.cpp
		test_filemapper.cpp
		test_game.cpp
		test_installer.cpp
		test_iplugin.cpp
		test_lifetime.cpp
)
mo2_target_sources(runner-tests
	FOLDER src/mocks
	PRIVATE
		../mocks/DummyFileTree.h
		../mocks/MockOrganizer.h
)
mo2_target_sources(runner-tests
	FOLDER src/plugins
	PRIVATE
		plugins/dummy-diagnose.py
		plugins/dummy-filemapper.py
		plugins/dummy-game.py
		plugins/dummy-installer.py
		plugins/dummy-iplugin.py
)
mo2_configure_tests(runner-tests NO_SOURCES WARNINGS 4)

set_target_properties(runner-tests PROPERTIES FOLDER tests/runner)

# link to runner
target_link_libraries(runner-tests PUBLIC runner)

# linking to Python - this is not required to get proper linking but required so that
# CMake generator variables will lookup appropriate DLLs for Python and update PATH
# accordingly thanks to mo2_configure_tests
target_link_libraries(runner-tests PUBLIC Python::Python)

# add mocks
target_include_directories(runner-tests
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../mocks)

set(PYLIB_DIR ${CMAKE_CURRENT_BINARY_DIR}/pylibs)
mo2_python_pip_install(runner-tests
	DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/pylibs
	PACKAGES
		pytest
		PyQt${MO2_QT_VERSION_MAJOR}==${MO2_PYQT_VERSION}
		PyQt${MO2_QT_VERSION_MAJOR}-Qt${MO2_QT_VERSION_MAJOR}==${MO2_QT_VERSION})

add_dependencies(runner-tests mobase)

set(PYTHONPATH "${PYLIB_DIR}\\;$<TARGET_FILE_DIR:mobase>\\;${Python_DLL_DIR}\\;${Python_LIB_DIR}")

set_tests_properties(${runner-tests_gtests}
    PROPERTIES
    ENVIRONMENT "PLUGIN_DIR=${CMAKE_CURRENT_SOURCE_DIR}/plugins;PYTHONPATH=${PYTHONPATH}"
)

cmake_minimum_required(VERSION 3.16)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets)

mo2_find_python_executable(PYTHON_EXE)

add_library(pybind11-qt STATIC)
mo2_configure_target(pybind11-qt
	NO_SOURCES
	WARNINGS 4
	EXTERNAL_WARNINGS 4
	AUTOMOC OFF
	TRANSLATIONS OFF
)
mo2_default_source_group()
target_sources(pybind11-qt
	PRIVATE
		./include/pybind11_qt/pybind11_qt_basic.h
		./include/pybind11_qt/pybind11_qt_containers.h
		./include/pybind11_qt/pybind11_qt_enums.h
		./include/pybind11_qt/pybind11_qt_holder.h
		./include/pybind11_qt/pybind11_qt_objects.h
		./include/pybind11_qt/pybind11_qt_qflags.h
		./include/pybind11_qt/pybind11_qt.h

		pybind11_qt_basic.cpp
		pybind11_qt_sip.cpp
		pybind11_qt_utils.cpp

)
mo2_target_sources(pybind11-qt
	FOLDER src/details
	PRIVATE
		./include/pybind11_qt/details/pybind11_qt_enum.h
		./include/pybind11_qt/details/pybind11_qt_qlist.h
		./include/pybind11_qt/details/pybind11_qt_qmap.h
		./include/pybind11_qt/details/pybind11_qt_sip.h
		./include/pybind11_qt/details/pybind11_qt_utils.h
)
target_link_libraries(pybind11-qt PUBLIC pybind11::pybind11 PRIVATE Qt6::Core Qt6::Widgets)
target_include_directories(pybind11-qt PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# this is kind of broken but it only works with this...
target_compile_definitions(pybind11-qt PUBLIC QT_NO_KEYWORDS)

# we need sip.h for pybind11-qt
add_custom_target(PyQt6-siph DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/sip.h")
set_target_properties(PyQt6-siph PROPERTIES FOLDER autogen)
add_custom_command(
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/sip.h"
	COMMAND ${Python_EXECUTABLE} -m sipbuild.tools.module
		--sip-h
		--target-dir ${CMAKE_CURRENT_BINARY_DIR}
		PyQt${MO2_QT_VERSION_MAJOR}.sip
)
add_dependencies(pybind11-qt PyQt6-siph)

target_include_directories(pybind11-qt PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
if(NOT MSVC)
	target_compile_options(pybind11-qt PRIVATE -Wno-attributes)
endif()

add_library(pybind11::qt ALIAS pybind11-qt)

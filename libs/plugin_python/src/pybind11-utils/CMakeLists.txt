cmake_minimum_required(VERSION 3.16)

add_library(pybind11-utils STATIC
	./include/pybind11_utils/functional.h
	./include/pybind11_utils/generator.h
	./include/pybind11_utils/shared_cpp_owner.h
	./include/pybind11_utils/smart_variant_wrapper.h
	./include/pybind11_utils/smart_variant.h

	functional.cpp
)
mo2_configure_target(pybind11-utils
	NO_SOURCES
	WARNINGS 4
	EXTERNAL_WARNINGS 4
	AUTOMOC OFF
	TRANSLATIONS OFF
)
mo2_default_source_group()
target_link_libraries(pybind11-utils PUBLIC pybind11::pybind11)
target_include_directories(pybind11-utils PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_library(pybind11::utils ALIAS pybind11-utils)

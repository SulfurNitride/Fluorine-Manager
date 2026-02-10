cmake_minimum_required(VERSION 3.16)

include(${CMAKE_CURRENT_LIST_DIR}/mo2_utils.cmake)

set(MO2_PYLIBS_DIR "${CMAKE_BINARY_DIR}/pylibs" CACHE PATH
    "default for path for Python libraries")

#! mo2_python_pip_install : run "pip install ..."
#
# \param:TARGET target to install Python package for
# \param:DIRECTORY directory to install libraries to, REQUIRED
# \param:PACKAGES packages to install, REQUIRED, can contain version constraints, e.g.,
#   "PyQt6==6.3.0"
#
function(mo2_python_pip_install TARGET)
	cmake_parse_arguments(MO2
		"NO_DEPENDENCIES;PRE_RELEASE;NO_FORCE;USE_CACHE" "DIRECTORY" "PACKAGES;EXTRA_INDEX_URLS" ${ARGN})

	mo2_set_if_not_defined(MO2_DIRECTORY ${MO2_PYLIBS_DIR})
	mo2_set_if_not_defined(MO2_NO_DEPENDENCIES OFF)
	mo2_set_if_not_defined(MO2_PRE_RELEASE OFF)
	mo2_set_if_not_defined(MO2_NO_FORCE OFF)
	mo2_set_if_not_defined(MO2_USE_CACHE OFF)
	mo2_set_if_not_defined(MO2_EXTRA_INDEX_URLS "")

	set(pip_install_arguments "")

	if (MO2_NO_DEPENDENCIES)
		list(APPEND pip_install_arguments --no-deps)
	endif()

	if (MO2_PRE_RELEASE)
		list(APPEND pip_install_arguments --pre)
	endif()

	if (NOT MO2_NO_FORCE)
		list(APPEND pip_install_arguments --force)
	endif()

	if (NOT USE_CACHE)
		list(APPEND pip_install_arguments --no-cache-dir)
	endif()

	foreach(_extra_index_url ${MO2_EXTRA_INDEX_URLS})
		list(APPEND pip_install_arguments --extra-index-url ${_extra_index_url})
	endforeach()

	mo2_find_python_executable(PYTHON_EXE)

	string(MAKE_C_IDENTIFIER "${MO2_PACKAGES}" PIP_FILE_LOG)
	set(pip_log_file "${CMAKE_CURRENT_BINARY_DIR}/${PIP_FILE_LOG}.log")

	add_custom_command(
		OUTPUT "${pip_log_file}"
		COMMAND ${PYTHON_EXE}
				-I
				-m pip
				install
				${pip_install_arguments}
				--upgrade
				--disable-pip-version-check
				--isolated
				--no-cache-dir
				--target="${MO2_DIRECTORY}"
				--log="${pip_log_file}"
				${MO2_PACKAGES}
	)

	set(pip_target_name "${TARGET}_pip_${PIP_FILE_LOG}")

	add_custom_target(${pip_target_name} ALL DEPENDS "${pip_log_file}")
	set_target_properties(${pip_target_name} PROPERTIES FOLDER autogen)

	add_dependencies(${TARGET} ${pip_target_name})
endfunction()

#! mo2_python_install_pyqt : install PyQt6 and create a PyQt6 target for it
#
# it is safe to call this function multiple times, PyQt6 will only be installed once
#
function(mo2_python_install_pyqt)
	if (TARGET PyQt6)
		return()
	endif()

	add_custom_target(PyQt6)
	set_target_properties(PyQt6 PROPERTIES FOLDER autogen)
	mo2_python_pip_install(PyQt6 NO_FORCE
		PACKAGES
			PyQt${MO2_QT_VERSION_MAJOR}==${MO2_PYQT_VERSION}
			sip==${MO2_SIP_VERSION})
endfunction()

#! mo2_python_uifiles : create .py files from .ui files for a python target
#
# \param:TARGET target to generate .py files for
# \param:INPLACE if specified, .py files are generated next to the .ui files, useful
#     for Python modules, otherwise files are generated in the binary directory
# \param:FILES list of .ui files to generate .py files from
#
function(mo2_python_uifiles TARGET)
	cmake_parse_arguments(MO2 "INPLACE" "" "FILES" ${ARGN})

	if (NOT MO2_FILES)
		return()
	endif()

	mo2_find_python_executable(PYTHON_EXE)
	mo2_python_install_pyqt()

	message(DEBUG "generating .py from ui files: ${MO2_FILES}")

	set(pyui_files "")
	foreach (UI_FILE ${MO2_FILES})
		get_filename_component(name "${UI_FILE}" NAME_WLE)
		if (${MO2_INPLACE})
			get_filename_component(folder "${UI_FILE}" DIRECTORY)
		else()
			set(folder "${CMAKE_CURRENT_BINARY_DIR}")
		endif()

		set(output "${folder}/${name}.py")
		add_custom_command(
			OUTPUT "${output}"
			COMMAND ${CMAKE_COMMAND} -E env PYTHONPATH=${CMAKE_BINARY_DIR}/pylibs
				${MO2_PYLIBS_DIR}/bin/pyuic${MO2_QT_VERSION_MAJOR}.exe
				-o "${output}"
				"${UI_FILE}"
			DEPENDS "${UI_FILE}"
		)

		list(APPEND pyui_files "${output}")
	endforeach()

	if (${MO2_INPLACE})
		source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}
			PREFIX autogen FILES ${pyui_files})
	endif()

	add_custom_target("${TARGET}_uic" DEPENDS ${pyui_files})
	set_target_properties("${TARGET}_uic" PROPERTIES FOLDER autogen)

	add_dependencies(${TARGET} "${TARGET}_uic")

	add_dependencies("${TARGET}_uic" PyQt6)

endfunction()

#! mo2_python_requirements : install requirements for a python target
#
# \param:TARGET target to install requirements for
# \param:LIBDIR library to install requirements to
#
function(mo2_python_requirements TARGET)
	cmake_parse_arguments(MO2 "" "LIBDIR" "" ${ARGN})

	mo2_find_python_executable(PYTHON_EXE)
	add_custom_command(
		OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/pip.log"
		COMMAND ${PYTHON_EXE}
				-I
				-m pip
				install --force --upgrade --disable-pip-version-check
				--target="${MO2_LIBDIR}"
				--log="${CMAKE_CURRENT_BINARY_DIR}/pip.log"
				-r "${PROJECT_SOURCE_DIR}/plugin-requirements.txt"
		DEPENDS "${PROJECT_SOURCE_DIR}/plugin-requirements.txt"
	)
	add_custom_target("${TARGET}_libs"
		ALL DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/pip.log")
	set_target_properties("${TARGET}_libs" PROPERTIES FOLDER autogen)

	add_dependencies(${TARGET} "${TARGET}_libs")

	file(MAKE_DIRECTORY "${MO2_LIBDIR}")

	install(
		DIRECTORY "${MO2_LIBDIR}"
		DESTINATION "${MO2_INSTALL_BIN}/plugins/${TARGET}/"
		PATTERN "__pycache__" EXCLUDE
	)

endfunction()

#! mo2_configure_python_module : configure a Python plugin module
#
# \param:TARGET target for the Python plugin
# \param:LIBDIR directory to install requirements (if any) to, default is "lib"
# \param:RESDIR directory to install genereated resources (if any) to, default is "res"
#
function(mo2_configure_python_module TARGET)
    cmake_parse_arguments(MO2 "" "LIBDIR;RESDIR" "" ${ARGN})

    mo2_set_if_not_defined(MO2_LIBDIR "lib")
    mo2_set_if_not_defined(MO2_RESDIR "res")

    set(res_dir "${PROJECT_SOURCE_DIR}/${MO2_RESDIR}")
    set(lib_dir "${PROJECT_SOURCE_DIR}/${MO2_LIBDIR}")

	# py files
	file(GLOB_RECURSE py_files CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.py)

	set(all_src_files ${py_files} ${ui_files} ${qrc_files})

	set(src_files ${all_src_files})
	list(FILTER src_files EXCLUDE REGEX "${lib_dir}[/\\].*")

	set(lib_files ${all_src_files})
	list(FILTER lib_files INCLUDE REGEX "${lib_dir}[/\\].*")

	target_sources(${TARGET} PRIVATE ${src_files})
	source_group(cmake FILES CMakeLists.txt)
	source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}
		PREFIX src
		FILES ${src_files})
	source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}
		PREFIX ${MO2_LIBDIR}
		FILES ${lib_files})

	# ui files
	file(GLOB_RECURSE ui_files CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.ui)
	mo2_python_uifiles(${TARGET} INPLACE FILES ${ui_files})

    # install requirements if there are any
	if(EXISTS "${PROJECT_SOURCE_DIR}/plugin-requirements.txt")
		mo2_python_requirements(${TARGET} LIBDIR "${lib_dir}")
		target_sources(${TARGET} PRIVATE
			"${PROJECT_SOURCE_DIR}/plugin-requirements.txt"
		)
		source_group(requirements
			FILES "${PROJECT_SOURCE_DIR}/plugin-requirements.txt")
	endif()

    set(install_dir "${MO2_INSTALL_BIN}/plugins/${TARGET}")

	# directories that go in bin/plugins/${name}
	install(
		DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/"
		DESTINATION ${install_dir}
		FILES_MATCHING PATTERN "*.py"
		PATTERN ".git" EXCLUDE
		PATTERN ".github" EXCLUDE
		PATTERN ".tox" EXCLUDE
		PATTERN ".mypy_cache" EXCLUDE
		PATTERN "vsbuild" EXCLUDE)

	# copy the resource directory if it exists
	if(EXISTS "${res_dir}")
		install(
			DIRECTORY "${res_dir}"
			DESTINATION ${install_dir}
		)
	endif()

endfunction()

#! mo2_configure_python_simple : configure a Python plugin (simple file)
#
# \param:TARGET target for the Python plugin
#
function(mo2_configure_python_simple TARGET)

	# this copies all the .py files that are directly in src/ into
	# ${install_dir}/
	#
	# any folder that contains at least one .py file (recursive) is copied in
	# bin/plugins/data
	#

	# ui files
	file(GLOB ui_files CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.ui)
	mo2_python_uifiles(${TARGET} FILES ${ui_files})

	# .py files directly in the directory
	file(GLOB py_files CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.py)

	# .json files directly in the directory
	file(GLOB json_files CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.json)

	file(GLOB_RECURSE extra_py_files CONFIGURE_DEPENDS
		RELATIVE  ${CMAKE_CURRENT_SOURCE_DIR}
		${CMAKE_CURRENT_SOURCE_DIR}/**/*.py)

	set(src_files ${py_files} ${ui_files} ${json_files} ${extra_py_files})
	target_sources(${TARGET} PRIVATE ${src_files})
	source_group(cmake FILES CMakeLists.txt)
	source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}
		PREFIX src
		FILES ${src_files})
	source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}
		PREFIX data
		FILES ${json_files})

    set(install_dir "${MO2_INSTALL_BIN}/plugins")

	# .py files directly in src/ go to plugins/
	install(FILES ${py_files} DESTINATION ${install_dir})

	# folders with Python files go into plugins/data
	set(extra_py_dirs ${extra_py_files})
	list(TRANSFORM extra_py_dirs REPLACE "[/\\][^/\\]+" "")
	list(REMOVE_DUPLICATES extra_py_dirs)

	install(DIRECTORY ${extra_py_dirs}
		DESTINATION "${install_dir}/data"
		FILES_MATCHING PATTERN "*.py")

	# JSON file go in plugins/data
	install(FILES ${json_files} DESTINATION "${install_dir}/data")

	# generated files go in plugins/data
	install(
		DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/"
		DESTINATION "${install_dir}/data"
		FILES_MATCHING
		PATTERN "*.py"
		PATTERN "CMakeFiles" EXCLUDE
		PATTERN "x64" EXCLUDE)

endfunction()

#! mo2_configure_python : configure a MO2 python target
#
# \param:MODULE indicates if this is a Python module plugin or a file plugin
# \param:TRANSLATIONS ON to generate translations (default), OFF to not generate them
# \param:LIB only for Python module, see mo2_configure_python_module
# \param:RES only for Python module, see mo2_configure_python_module
#
function(mo2_configure_python TARGET)
    cmake_parse_arguments(MO2 "MODULE;SIMPLE" "TRANSLATIONS;LIB;RES" "" ${ARGN})

	mo2_set_if_not_defined(MO2_TRANSLATIONS ON)

	if ((${MO2_MODULE} AND ${MO2_SIMPLE}) OR (NOT(${MO2_MODULE}) AND NOT(${MO2_SIMPLE})))
		message(FATAL_ERROR "mo2_configure_python should be called with either SIMPLE or MODULE")
	endif()

    if (${MO2_MODULE})
        mo2_configure_python_module(${TARGET} ${ARGN})
    else()
        mo2_configure_python_simple(${TARGET} ${ARGN})
    endif()

	# do this AFTER configure_ to properly handle the the ui files
	if(${MO2_TRANSLATIONS})
        mo2_add_translations(${TARGET} SOURCES ${CMAKE_CURRENT_SOURCE_DIR})
    endif()

	file(GLOB_RECURSE py_files CONFIGURE_DEPENDS *.py)
	file(GLOB_RECURSE rc_files CONFIGURE_DEPENDS *.rc)
	file(GLOB_RECURSE ui_files CONFIGURE_DEPENDS *.ui)

	target_sources(${TARGET}
		PRIVATE ${py_files} ${ui_files} ${rc_files} ${qm_files})

endfunction()

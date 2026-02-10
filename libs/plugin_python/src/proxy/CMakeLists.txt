cmake_minimum_required(VERSION 3.16)

if(NOT TARGET mo2::uibase)
  find_package(mo2-uibase CONFIG REQUIRED)
endif()

set(PLUGIN_NAME "plugin_python")

add_library(proxy SHARED proxypython.cpp proxypython.h)
mo2_configure_plugin(proxy
    NO_SOURCES
    WARNINGS 4
    EXTERNAL_WARNINGS 4
    TRANSLATIONS OFF
    EXTRA_TRANSLATIONS
    ${CMAKE_CURRENT_SOURCE_DIR}/../runner
    ${CMAKE_CURRENT_SOURCE_DIR}/../mobase
    ${CMAKE_CURRENT_SOURCE_DIR}/../pybind11-qt)
mo2_default_source_group()
target_link_libraries(proxy PRIVATE runner mo2::uibase)
if(NOT WIN32)
  target_compile_definitions(proxy PRIVATE
    MO2_PYTHON_PURELIB_DIR="${Python_PURELIB_DIR}"
    MO2_PYTHON_PLATLIB_DIR="${Python_PLATLIB_DIR}")
endif()
set_target_properties(proxy PROPERTIES OUTPUT_NAME ${PLUGIN_NAME})
mo2_install_plugin(proxy FOLDER)

set(PLUGIN_PYTHON_DIR bin/plugins/${PLUGIN_NAME})

# install runner
if(WIN32)
  target_link_options(proxy PRIVATE "/DELAYLOAD:runner.dll")
  install(FILES $<TARGET_FILE:runner> DESTINATION ${PLUGIN_PYTHON_DIR}/dlls)
endif()

# translations (custom location)
mo2_add_translations(proxy
    TS_FILE ${CMAKE_CURRENT_SOURCE_DIR}/../${PLUGIN_NAME}_en.ts
    SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../runner
    ${CMAKE_CURRENT_SOURCE_DIR}/../mobase
    ${CMAKE_CURRENT_SOURCE_DIR}/../pybind11-qt)

# install DLLs files needed
set(DLL_DIRS ${PLUGIN_PYTHON_DIR}/dlls)
if(WIN32)
  file(GLOB dlls_to_install
	  ${Python_HOME}/dlls/libffi*.dll
	  ${Python_HOME}/dlls/sqlite*.dll
	  ${Python_HOME}/dlls/libssl*.dll
	  ${Python_HOME}/dlls/libcrypto*.dll
	  ${Python_HOME}/python${Python_VERSION_MAJOR}*.dll)
  install(FILES ${dlls_to_install} DESTINATION ${DLL_DIRS})
endif()

# install Python .pyd files
set(PYLIB_DIR ${PLUGIN_PYTHON_DIR}/libs)
if(WIN32)
  file(GLOB libs_to_install ${Python_DLL_DIR}/*.pyd)
  install(FILES ${libs_to_install} DESTINATION ${PYLIB_DIR})
endif()

# generate + install standard library
if(WIN32)
  set(pythoncore_zip "${CMAKE_CURRENT_BINARY_DIR}/pythoncore.zip")
  add_custom_command(
      TARGET proxy POST_BUILD
      COMMAND ${Python_EXECUTABLE}
          "${CMAKE_CURRENT_SOURCE_DIR}/build_pythoncore.py"
          ${pythoncore_zip}
      )
  install(FILES ${pythoncore_zip} DESTINATION ${PYLIB_DIR})
endif()

# install mobase
install(TARGETS mobase DESTINATION ${PYLIB_DIR})

# install PyQt6
if(WIN32)
  install(
      DIRECTORY ${CMAKE_BINARY_DIR}/pylibs/PyQt${MO2_QT_VERSION_MAJOR}
      DESTINATION ${PYLIB_DIR}
      PATTERN "*.pyd"
      PATTERN "*.pyi"
      PATTERN "__pycache__" EXCLUDE
      PATTERN "bindings" EXCLUDE
      PATTERN "lupdate" EXCLUDE
      PATTERN "Qt6" EXCLUDE
      PATTERN "uic" EXCLUDE
  )
endif()

if(NOT WIN32)
  # Build-tree runtime layout expected by proxypython.cpp.
  add_custom_command(TARGET proxy POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/src/src/plugins"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/src/src/plugins/dlls"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/src/src/plugins/libs"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/src/src/plugins/plugin_python/dlls"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/src/src/plugins/plugin_python/libs"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "$<TARGET_FILE:proxy>"
          "${CMAKE_BINARY_DIR}/src/src/plugins/$<TARGET_FILE_NAME:proxy>"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "$<TARGET_FILE:runner>"
          "${CMAKE_BINARY_DIR}/src/src/plugins/dlls/$<TARGET_FILE_NAME:runner>"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "$<TARGET_FILE:runner>"
          "${CMAKE_BINARY_DIR}/src/src/plugins/plugin_python/dlls/$<TARGET_FILE_NAME:runner>"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "$<TARGET_FILE:mobase>"
          "${CMAKE_BINARY_DIR}/src/src/plugins/libs/$<TARGET_FILE_NAME:mobase>"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "$<TARGET_FILE:mobase>"
          "${CMAKE_BINARY_DIR}/src/src/plugins/plugin_python/libs/$<TARGET_FILE_NAME:mobase>")
endif()

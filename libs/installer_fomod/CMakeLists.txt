cmake_minimum_required(VERSION 3.16)

project(installer_fomod)

find_package(Qt6 COMPONENTS Widgets REQUIRED)

# Compatibility helpers for this monorepo build (upstream expects mo2-cmake package).
if(NOT COMMAND mo2_configure_plugin)
  function(mo2_configure_plugin target)
    set_target_properties(${target} PROPERTIES
      AUTOMOC ON
      AUTOUIC ON
      AUTORCC ON
      CXX_STANDARD 23
      CXX_STANDARD_REQUIRED ON
      POSITION_INDEPENDENT_CODE ON
    )

    target_compile_options(${target} PRIVATE
      "-idirafter" "${CMAKE_SOURCE_DIR}/libs/uibase/include/uibase"
    )
    set_property(TARGET ${target} APPEND PROPERTY AUTOMOC_MOC_OPTIONS
      "-I${CMAKE_SOURCE_DIR}/libs/uibase/include/uibase"
    )
  endfunction()
endif()

if(NOT COMMAND mo2_install_plugin)
  function(mo2_install_plugin target)
    install(TARGETS ${target}
      LIBRARY DESTINATION plugins
    )
  endfunction()
endif()

add_subdirectory(src)

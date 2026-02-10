cmake_minimum_required(VERSION 3.16)

find_package(Qt6 COMPONENTS Widgets REQUIRED)
find_package(ZLIB REQUIRED)

# Try cmake config first, fall back to pkg-config
find_package(lz4 CONFIG QUIET)
if(NOT lz4_FOUND)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LZ4 REQUIRED IMPORTED_TARGET liblz4)
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)

add_library(game_gamebryo STATIC
    dummybsa.cpp
    dummybsa.h
    gamebryobsainvalidation.cpp
    gamebryobsainvalidation.h
    gamebryodataarchives.cpp
    gamebryodataarchives.h
    gamebryogameplugins.cpp
    gamebryogameplugins.h
    gamebryolocalsavegames.cpp
    gamebryolocalsavegames.h
    gamebryomoddatachecker.cpp
    gamebryomoddatachecker.h
    gamebryomoddatacontent.cpp
    gamebryomoddatacontent.h
    gamebryosavegame.cpp
    gamebryosavegame.h
    gamebryosavegameinfo.cpp
    gamebryosavegameinfo.h
    gamebryosavegameinfowidget.cpp
    gamebryosavegameinfowidget.h
    gamebryosavegameinfowidget.ui
    gamebryoscriptextender.cpp
    gamebryoscriptextender.h
    gamebryounmanagedmods.cpp
    gamebryounmanagedmods.h
    gamegamebryo.cpp
    gamegamebryo.h
    vdf_parser.h
)

target_link_libraries(game_gamebryo
    PUBLIC
        uibase
        game_features
        Qt6::Widgets
    PRIVATE
        ZLIB::ZLIB
        $<IF:$<TARGET_EXISTS:lz4::lz4>,lz4::lz4,PkgConfig::LZ4>
)

# NaK FFI for game detection (replaces Windows registry)
if(TARGET mo2::nak_ffi)
    target_link_libraries(game_gamebryo PRIVATE mo2::nak_ffi)
    target_compile_definitions(game_gamebryo PRIVATE HAS_NAK_FFI)
endif()

target_include_directories(game_gamebryo PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Keep uibase bare includes after system headers to avoid strings.h collision.
target_compile_options(game_gamebryo PRIVATE
    "-idirafter" "${CMAKE_SOURCE_DIR}/libs/uibase/include/uibase"
)

# AUTOMOC does not reliably honor -idirafter, so provide the bare uibase include
# path explicitly for moc parsing of Q_INTERFACES declarations.
set_property(TARGET game_gamebryo APPEND PROPERTY AUTOMOC_MOC_OPTIONS
    "-I${CMAKE_SOURCE_DIR}/libs/uibase/include/uibase"
)

cmake_minimum_required(VERSION 3.16)

find_package(Boost CONFIG REQUIRED COMPONENTS algorithm interprocess filesystem)
find_package(spdlog CONFIG REQUIRED)

file(GLOB sources "*.cpp" "*.h")
source_group("" FILES ${sources})

add_library(shared STATIC ${sources})
target_link_libraries(shared
    PUBLIC
    Boost::algorithm Boost::interprocess comsuppw
    Boost::filesystem spdlog::spdlog_header_only)
target_include_directories(shared
    PUBLIC ${PROJECT_SOURCE_DIR}/include/usvfs ${CMAKE_CURRENT_SOURCE_DIR})
target_precompile_headers(shared PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/pch.h)
target_compile_options(shared PUBLIC /FI"pch.h")
target_compile_definitions(shared PUBLIC
    SPDLOG_USE_STD_FORMAT SPDLOG_NO_NAME SPDLOG_NO_REGISTRY_MUTEX
    BOOST_INTERPROCESS_SHARED_DIR_FUNC BOOST_CONFIG_SUPPRESS_OUTDATED_MESSAGE
    NOMINMAX _WINDOWS _UNICODE UNICODE
    _DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR
)

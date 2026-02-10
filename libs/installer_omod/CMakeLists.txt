# CMake 3.18 due to https://gitlab.kitware.com/cmake/cmake/-/issues/20764
cmake_minimum_required(VERSION 3.18)

# set globally as Nuget gets confused about ZERO_CHECK, ALL_BUILD and INSTALL otherwise
set(CMAKE_DOTNET_TARGET_FRAMEWORK_VERSION "v4.8")

project(installer_omod LANGUAGES CXX CSharp)

add_subdirectory(src)

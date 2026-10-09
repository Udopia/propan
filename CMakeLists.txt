cmake_minimum_required(VERSION 3.20)

project(propan
    VERSION 0.1.0
    DESCRIPTION "Boolean Constraint Propagation Library"
    LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(PROPAN_BUILD_APPS "Build applications (slursat, ...)" ${PROJECT_IS_TOP_LEVEL})

find_package(LibArchive REQUIRED)

# Header-only core library
add_library(propan INTERFACE)
add_library(propan::propan ALIAS propan)
target_include_directories(propan INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_compile_features(propan INTERFACE cxx_std_20)
target_link_libraries(propan INTERFACE LibArchive::LibArchive)

# Warning flags for targets built within this project (not propagated to consumers)
add_library(propan_warnings INTERFACE)
target_compile_options(propan_warnings INTERFACE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra>)

if(PROPAN_BUILD_APPS)
    add_subdirectory(apps)
endif()

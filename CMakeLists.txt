cmake_minimum_required(VERSION 3.9)
project(event_horizon)

set(CMAKE_C_STANDARD 99)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_SHARED_LIBRARY_PREFIX "")

option(EVH_VENDOR_LIBUV "Build libuv from source and statically link to it" OFF)

# Set default build type if not specified
get_property(IS_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if (NOT IS_MULTI_CONFIG AND NOT CMAKE_BUILD_TYPE)
    message(STATUS "Setting build type to 'Release' as none was specified.")
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build." FORCE)
endif()

# Check for link time optimization support
if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    include(CheckIPOSupported)
    check_ipo_supported(RESULT LTO)
    if(LTO)
        message(STATUS "${CMAKE_PROJECT_NAME} link-time optimization enabled")
    endif()
endif()

# Find libraries on system
find_package(JStar 1 REQUIRED)
include(${CMAKE_SOURCE_DIR}/cmake/Findlibuv.cmake)

# include external dependencies
add_subdirectory(extern)

# Include the source
add_subdirectory(src)

cmake_minimum_required(VERSION 3.21)
project(event_horizon)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_SHARED_LIBRARY_PREFIX "")

option(EVH_VENDOR_LIBUV   "Build libuv from source and statically link to it"   OFF)
option(EVH_VENDOR_MBEDTLS "Build mbedTLS from source and statically link to it" OFF)

# Set default build type if not specified
get_property(IS_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if (PROJECT_IS_TOP_LEVEL AND NOT IS_MULTI_CONFIG AND NOT CMAKE_BUILD_TYPE)
    message(STATUS "Setting build type to 'Release' as none was specified.")
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build." FORCE)
endif()

# Check for link time optimization support
if(PROJECT_IS_TOP_LEVEL AND NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    include(CheckIPOSupported)
    check_ipo_supported(RESULT LTO)
    if(LTO)
        message(STATUS "event_horizon link-time optimization enabled")

        # For mbedTLS
        cmake_policy(SET CMP0069 NEW)
        set(CMAKE_POLICY_DEFAULT_CMP0069 NEW)

        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_MINSIZEREL ON)
    endif()
endif()

if(PROJECT_IS_TOP_LEVEL)
    add_compile_options(
        "$<$<OR:$<C_COMPILER_ID:GNU>,$<C_COMPILER_ID:Clang>>:-Wall;-Wextra;-Werror=switch>"
        "$<$<AND:$<CONFIG:Release>,$<C_COMPILER_ID:GNU>>:-fno-plt>"
        "$<$<C_COMPILER_ID:MSVC>:/W3;/wd4244;/wd4267;/wd5105;/wd4116>"
    )
    add_link_options(
        $<$<AND:$<CONFIG:Release>,$<OR:$<C_COMPILER_ID:GNU>,$<C_COMPILER_ID:Clang>>>:-s>
    )

    set(MSVC_LIKE
        "$<OR:$<C_COMPILER_ID:MSVC>,$<AND:$<C_COMPILER_ID:Clang>,$<STREQUAL:${CMAKE_C_SIMULATE_ID},MSVC>>>"
    )
    add_compile_definitions(
        $<$<PLATFORM_ID:Windows>:WIN32_LEAN_AND_MEAN>
        $<$<PLATFORM_ID:Windows>:NOMINMAX>
        $<${MSVC_LIKE}:_CRT_SECURE_NO_WARNINGS>
    )
endif()

# Find libraries on system
find_package(JStar 1 REQUIRED)
include(${CMAKE_SOURCE_DIR}/cmake/Findlibuv.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/FindMbedTLS.cmake)

# include external dependencies
add_subdirectory(extern)

# Include the source
add_subdirectory(src)

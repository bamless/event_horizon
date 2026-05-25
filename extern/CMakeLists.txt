find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${PROJECT_SOURCE_DIR}/.git")
    # Update submodules as needed
    option(GIT_SUBMODULE "Check submodules during build" ON)
    if(GIT_SUBMODULE)
        message(STATUS "Submodule update")
        execute_process(COMMAND ${GIT_EXECUTABLE} submodule update --init --recursive
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            RESULT_VARIABLE GIT_SUBMOD_RESULT)
        if(NOT GIT_SUBMOD_RESULT EQUAL "0")
            message(FATAL_ERROR "git submodule update --init failed with ${GIT_SUBMOD_RESULT}, please checkout submodules")
        endif()
    endif()
endif()

# Libuv target
if(NOT LIBUV_FOUND OR EVH_VENDOR_LIBUV)
    # build from source
    add_subdirectory(libuv EXCLUDE_FROM_ALL)
    set_property(TARGET uv_a PROPERTY POSITION_INDEPENDENT_CODE ON)
    # rename static target to `libuv`
    add_library(libuv INTERFACE IMPORTED GLOBAL)
    target_link_libraries(libuv INTERFACE uv_a)
else()
    # use libuv from system
    add_library(libuv SHARED IMPORTED GLOBAL)
    set_target_properties(libuv PROPERTIES IMPORTED_LOCATION "${LIBUV_LIBRARIES}")
    target_include_directories(libuv INTERFACE "${LIBUV_INCLUDE_DIR}")
endif()

# MbedTLS target
if(NOT MbedTLS_FOUND OR EVH_VENDOR_MBEDTLS)
    # Build only the library components; skip programs, tests and fatal
    # warnings. Static libraries are ON by default in mbedTLS's own CMake.
    set(ENABLE_PROGRAMS        OFF CACHE BOOL "" FORCE)
    set(ENABLE_TESTING         OFF CACHE BOOL "" FORCE)
    set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
    # Set PIC globally before entering the subdirectory so every internal
    # target (mbedtls, mbedx509, tfpsacrypto and its sub-targets) picks it
    # up automatically — this version of mbedTLS nests tf-psa-crypto, making
    # per-target enumeration fragile.
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    add_subdirectory(mbedtls EXCLUDE_FROM_ALL)
    # The `MbedTLS::mbedtls` static target already carries mbedx509 and
    # tfpsacrypto as transitive link dependencies, so consumers only need one.
    add_library(libmbedtls INTERFACE IMPORTED GLOBAL)
    target_link_libraries(libmbedtls INTERFACE MbedTLS::mbedtls)
    target_include_directories(libmbedtls INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/include)
else()
    # Use the three mbedTLS libraries found on the system
    add_library(libmbedtls INTERFACE IMPORTED GLOBAL)
    target_link_libraries(libmbedtls INTERFACE ${MBEDTLS_LIBRARIES})
    target_include_directories(libmbedtls INTERFACE ${MBEDTLS_INCLUDE_DIRS})
endif()

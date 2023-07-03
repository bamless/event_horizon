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

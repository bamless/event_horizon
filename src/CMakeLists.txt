add_library(event_horizon SHARED 
    callbacks.h
    callbacks.c
    consts.h
    consts.c
    dns.h
    dns.c
    errors.h
    errors.c
    event_horizon.c
    event_horizon.h
    event_loop.h
    event_loop.c
    handle.h
    handle.c
    idle.h
    idle.c
    pipe.h
    pipe.c
    stream.h
    stream.c
    tcp.c
    tcp.h
    udp.h
    udp.c
    timer.h
    timer.c
    sock_utils.h
    sock_utils.c
)

set_target_properties(event_horizon PROPERTIES C_VISIBILITY_PRESET hidden)
set_target_properties(event_horizon PROPERTIES OUTPUT_NAME "__package__")

target_link_libraries(event_horizon PUBLIC jstar::jstar libuv)
target_compile_definitions(event_horizon PRIVATE jstar_EXPORTS)
target_compile_options(event_horizon
    PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W3>
        $<$<CXX_COMPILER_ID:Clang>:-Wall -Wextra>
        $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra>
)

if(LTO)
    set_target_properties(event_horizon PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

# Copy the built shared librarty to the `event_horizon/uv` folder
add_custom_command(TARGET event_horizon POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:event_horizon> 
            ${PROJECT_SOURCE_DIR}/event_horizon/uv
)

# Install event_horizon
include(GNUInstallDirs)
install(DIRECTORY
    ${PROJECT_SOURCE_DIR}/event_horizon
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/jstar
)

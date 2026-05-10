# =============================================================================
# Examples for how to use XDG C++ APIs
# =============================================================================

function(xdg_add_examples name source)
    add_executable(${name} ${source})
    if (XDG_ENABLE_LIBMESH)
        target_link_libraries(${test} PRIVATE ${LIBMESH_LINK_LIBRARIES})
    endif()
    if (XDG_ENABLE_GPRT)
        target_link_libraries(${test} PRIVATE $<BUILD_INTERFACE:gprt>)
    endif()
    if (XDG_ENABLE_MOAB)
        target_link_libraries(${test} PRIVATE MOAB)
    endif()
endfunction()


xdg_add_examples(ray_segments_example ray_segments.cpp)
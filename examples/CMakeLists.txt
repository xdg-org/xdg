# =============================================================================
# Examples for how to use XDG C++ APIs
# =============================================================================

function(xdg_add_examples name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE xdg fmt::fmt)
    if (XDG_ENABLE_LIBMESH)
        target_link_libraries(${name} PRIVATE ${LIBMESH_LINK_LIBRARIES})
    endif()
    if (XDG_ENABLE_GPRT)
        target_link_libraries(${name} PRIVATE $<BUILD_INTERFACE:gprt>)
    endif()
    if (XDG_ENABLE_MOAB)
        target_include_directories(${name} PRIVATE $<TARGET_PROPERTY:MOAB,INTERFACE_INCLUDE_DIRECTORIES>)
        target_link_libraries(${name} PRIVATE MOAB)
    endif()
    set_target_properties(${name} PROPERTIES
            BUILD_RPATH "$<TARGET_FILE_DIR:xdg>")
endfunction()


xdg_add_examples(ray_segments_example ray_segments.cpp)

set(EXAMPLE_MESH_FILES
        cube.h5m
        brick.exo
)

foreach(file ${EXAMPLE_MESH_FILES})
    ADD_CUSTOM_TARGET(example_${file} ALL
            COMMAND ${CMAKE_COMMAND} -E create_symlink
            "${CMAKE_SOURCE_DIR}/tests/test_files/${file}"
            "${CMAKE_CURRENT_BINARY_DIR}/${file}")
endforeach()
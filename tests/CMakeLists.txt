set(
TEST_NAMES
test_bbox
test_boundary_faces
test_bvh
test_config
test_closest
test_find_element
test_mesh_internal
test_occluded
test_ray_fire
test_point_in_volume
test_normal
test_measure
test_no_geom
test_ray_duals
test_xdg_interface
test_tet_containment
test_hex_containment
test_tracks
test_walk_elements
test_tally_segments
test_mesh_connectivity
)

if (XDG_ENABLE_MOAB)
    list(APPEND TEST_NAMES test_moab)
endif()

if (XDG_ENABLE_LIBMESH)
    list(APPEND TEST_NAMES test_libmesh)
endif()

if (XDG_ENABLE_MOAB AND XDG_ENABLE_LIBMESH)
    list(APPEND TEST_NAMES test_mesh_library_cross_check)
endif()

if (XDG_ENABLE_MOAB)
    list(APPEND TEST_NAMES test_ray_tracer_cross_check)
endif()

if (XDG_ENABLE_MOAB AND XDG_BUILD_TOOLS)
    list(APPEND TEST_NAMES test_overlap_check)
endif()

foreach(test ${TEST_NAMES})
    add_executable(${test} test_main.cpp ${test}.cpp)
    target_link_libraries(${test} PRIVATE xdg fmt::fmt Catch2::Catch2)
    if (XDG_ENABLE_LIBMESH)
        target_link_libraries(${test} PRIVATE ${LIBMESH_LINK_LIBRARIES})
    endif()
    if (XDG_ENABLE_GPRT)
        target_link_libraries(${test} PRIVATE $<BUILD_INTERFACE:gprt>)
    endif()
    if (XDG_ENABLE_MOAB)
        target_link_libraries(${test} PRIVATE MOAB)
    endif()
    set_target_properties(${test} PROPERTIES
                                  BUILD_RPATH "$<TARGET_FILE_DIR:xdg>")
    catch_discover_tests(${test}
        PROPERTIES
        SKIP_RETURN_CODE 4
        TEST_PREFIX "${test}::"
        EXTRA_ARGS "--reporter;console;--out=/dev/stdout")
endforeach()

if (TARGET test_overlap_check)
    target_sources(test_overlap_check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../tools/overlap.cpp)
    target_include_directories(test_overlap_check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../tools)
    target_link_libraries(test_overlap_check PRIVATE indicators::indicators)
endif()


set(
TEST_FILES
pwr_pincell.h5m
cube.h5m
cube-mesh-no-geom.h5m
cube-mesh-no-geom.exo
cube-w-multiblock-sideset.exo
cube-w-multiblock-sideset.h5m
cyl-brick-quads.h5m
cyl-brick-quads.exo
no_overlap.h5m
overlap.h5m
overlap-edge.h5m
beam-overlaps.h5m
small_overlap.h5m
no_overlap_imp.h5m
enclosed.h5m
cyl-brick.exo
cyl-brick.h5m
brick.exo
brick-sidesets.exo
jezebel.exo
jezebel.h5m
jezebel-quads.exo
jezebel-quads.h5m
pincell.h5m
pincell-implicit.exo
small-tet-mesh.h5m
small-tet-mesh.exo
single-tet.h5m
single-tet.exo
quad_sphere.h5m
)

foreach(file ${TEST_FILES})
    ADD_CUSTOM_TARGET(${file} ALL
                      COMMAND ${CMAKE_COMMAND} -E create_symlink
                      "${CMAKE_CURRENT_SOURCE_DIR}/test_files/${file}"
                      "${CMAKE_CURRENT_BINARY_DIR}/${file}")
endforeach()

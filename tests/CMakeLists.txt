set(
TEST_NAMES
test_bbox
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
test_tracks
test_tet_intersection
test_tally_segments
)

if (XDG_ENABLE_MOAB)
    list(APPEND TEST_NAMES test_moab)
endif()

if (XDG_ENABLE_LIBMESH)
    list(APPEND TEST_NAMES test_libmesh)
endif()

if (XDG_ENABLE_MOAB AND XDG_ENABLE_LIBMESH)
    list(APPEND TEST_NAMES test_cross_check)
endif()

# placing this last as it's a slow test
if (XDG_ENABLE_MOAB)
    list(APPEND TEST_NAMES test_overlap_check)
endif()

foreach(test ${TEST_NAMES})
    add_executable(${test} ${test}.cpp)
    target_link_libraries(${test} xdg Catch2::Catch2WithMain)
    set_target_properties(${test} PROPERTIES
                                  BUILD_RPATH "$<TARGET_FILE_DIR:xdg>")
    catch_discover_tests(${test}
        PROPERTIES
        SKIP_RETURN_CODE 4
        TEST_PREFIX "${test}::"
        EXTRA_ARGS "--reporter;console;--out=/dev/stdout")
endforeach()


set(
TEST_FILES
pwr_pincell.h5m
cube.h5m
cube-mesh-no-geom.h5m
cube-mesh-no-geom.exo
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
pincell.h5m
pincell-implicit.exo
small-tet-mesh.h5m
small-tet-mesh.exo
)

foreach(file ${TEST_FILES})
    ADD_CUSTOM_TARGET(${file} ALL
                      COMMAND ${CMAKE_COMMAND} -E create_symlink
                      "${CMAKE_CURRENT_SOURCE_DIR}/test_files/${file}"
                      "${CMAKE_CURRENT_BINARY_DIR}/${file}")
endforeach()

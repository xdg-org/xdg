set(TOOL_NAMES
particle_sim
ray_fire
find_volume
point_in_volume
overlap_check
walk_elements
tally_segments
)

foreach(tool ${TOOL_NAMES})
    string(REPLACE "_" "-" tool_exec ${tool})
    add_executable(${tool_exec} ${tool}.cpp)
    target_link_libraries(${tool_exec} xdg argparse indicators::indicators OpenMP::OpenMP_CXX)
    install(TARGETS ${tool_exec} DESTINATION ${CMAKE_INSTALL_BINDIR}/tools)
endforeach()
set(TOOL_NAMES
particle_sim
ray_fire
find_volume
point_in_volume
overlap_check
)

# Add GPRT tools
if (XDG_ENABLE_GPRT)
  list(APPEND TOOL_NAMES
    gprt_test
  )
endif()


foreach(tool ${TOOL_NAMES})
    string(REPLACE "_" "-" tool_exec ${tool})
    add_executable(${tool_exec} ${tool}.cpp)
    target_link_libraries(${tool_exec} xdg argparse)
    install(TARGETS ${tool_exec} DESTINATION ${CMAKE_INSTALL_BINDIR}/tools)
endforeach()

if (XDG_ENABLE_GPRT)
  add_subdirectory(gprt)
endif()
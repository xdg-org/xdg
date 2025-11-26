set(TOOL_NAMES
particle_sim
ray_fire
find_volume
point_in_volume
overlap_check
)

foreach(tool ${TOOL_NAMES})
    string(REPLACE "_" "-" tool_exec ${tool})
    add_executable(${tool_exec} ${tool}.cpp)
    target_link_libraries(${tool_exec} xdg argparse indicators::indicators)
    install(TARGETS ${tool_exec} DESTINATION ${CMAKE_INSTALL_BINDIR}/tools)
endforeach()


# Configure GPRT tools
if (XDG_ENABLE_GPRT)
  set(GPRT_TOOLS
    xdg_gprt_render_tool
  )

  foreach(tool ${GPRT_TOOLS})
    string(REPLACE "_" "-" tool_exec ${tool})
    add_executable(${tool_exec} ${tool}.cpp)
    target_link_libraries(${tool_exec} xdg argparse)
    install(TARGETS ${tool_exec} DESTINATION ${CMAKE_INSTALL_BINDIR}/tools)
    target_link_libraries(${tool_exec} xdg gprt)

    embed_devicecode(
      OUTPUT_TARGET
      ${tool}_deviceCode
      HEADERS
      ${CMAKE_CURRENT_SOURCE_DIR}/${tool}_shared.h
      SOURCES
      ${CMAKE_CURRENT_SOURCE_DIR}/${tool}_deviceCode.slang
    )

    target_link_libraries(${tool_exec}
      ${tool}_deviceCode
      gprt::gprt
    )
  endforeach()
endif()
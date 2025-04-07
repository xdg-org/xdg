


set(TOOL_NAMES
particle_sim
ray_fire
find_volume
point_in_volume
overlap_check
gprt_test
)

foreach(tool ${TOOL_NAMES})
    string(REPLACE "_" "-" tool_exec ${tool})
    add_executable(${tool_exec} ${tool}.cpp)
    target_link_libraries(${tool_exec} xdg argparse)
    install(TARGETS ${tool_exec} DESTINATION ${CMAKE_INSTALL_BINDIR}/tools)
endforeach()

# GPRT specific includes
target_link_libraries(gprt-test xdg gprt)

embed_devicecode(
  OUTPUT_TARGET
  gprt_test_deviceCode
  HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/sharedCode.h
  SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/deviceCode.slang
)

target_link_libraries(gprt-test
    gprt_test_deviceCode
    gprt::gprt
)
set(GPRT_TOOLS
  gprt_test
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
    ${CMAKE_CURRENT_SOURCE_DIR}/${tool}_sharedCode.h
  SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/${tool}_deviceCode.slang
  )

  target_link_libraries(${tool_exec}
    ${tool}_deviceCode
    gprt::gprt
  )
endforeach()

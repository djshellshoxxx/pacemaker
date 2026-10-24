set(UI_INPUT ${CMAKE_CURRENT_SOURCE_DIR}/ui/index.html)
set(UI_OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/UiData.h)
add_custom_command(
  OUTPUT ${UI_OUTPUT}
  COMMAND ${CMAKE_COMMAND} -DINPUT=${UI_INPUT} -DOUTPUT=${UI_OUTPUT} -P ${CMAKE_CURRENT_SOURCE_DIR}/embed.cmake
  DEPENDS ${UI_INPUT} ${CMAKE_CURRENT_SOURCE_DIR}/embed.cmake
  COMMENT "Embedding web UI")
add_library(pacemaker_host STATIC Host.cpp ${UI_OUTPUT})
target_include_directories(pacemaker_host PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(pacemaker_host PUBLIC pacemaker_outputs pacemaker_sim)
if(NOT MSVC)
  target_compile_options(pacemaker_host PRIVATE -Wall -Wextra)
endif()
add_executable(pacemaker_server main.cpp)
target_link_libraries(pacemaker_server PRIVATE pacemaker_host)

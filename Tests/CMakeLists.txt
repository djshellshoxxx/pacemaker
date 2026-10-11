# Test sources are globbed (*Tests.cpp) so modules can add test files without editing this file.
file(GLOB PACEMAKER_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*Tests.cpp)
add_executable(PacemakerTests TestMain.cpp ${PACEMAKER_TEST_SOURCES})
target_link_libraries(PacemakerTests PRIVATE pacemaker_engine pacemaker_core)
if(NOT MSVC)
  target_compile_options(PacemakerTests PRIVATE -Wall -Wextra)
endif()
add_test(NAME PacemakerTests COMMAND PacemakerTests)

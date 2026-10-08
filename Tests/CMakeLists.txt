add_executable(PacemakerTests
  TestMain.cpp
  EngineTests.cpp
  OutputTests.cpp
  CalibrationTests.cpp
  HostTests.cpp)
target_link_libraries(PacemakerTests PRIVATE pacemaker_engine pacemaker_outputs pacemaker_host pacemaker_sim)
if(NOT MSVC)
  target_compile_options(PacemakerTests PRIVATE -Wall -Wextra)
endif()
add_test(NAME PacemakerTests COMMAND PacemakerTests)

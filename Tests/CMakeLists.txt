add_executable(PacemakerTests
  TestMain.cpp
  EngineTests.cpp)
target_link_libraries(PacemakerTests PRIVATE pacemaker_engine)
if(NOT MSVC)
  target_compile_options(PacemakerTests PRIVATE -Wall -Wextra)
endif()
add_test(NAME PacemakerTests COMMAND PacemakerTests)

cmake_minimum_required(VERSION 3.22)
project(Pacemaker VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(PACEMAKER_WITH_LINK "Build Ableton Link output (used by later phases)" ON)
option(PACEMAKER_BUILD_TESTS "Build the test harness" ON)
option(PACEMAKER_BUILD_EVAL "Build the offline evaluation CLI" ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

add_subdirectory(engine)

if(PACEMAKER_BUILD_EVAL)
  add_subdirectory(tools/eval)
endif()

if(PACEMAKER_BUILD_TESTS)
  enable_testing()
  add_subdirectory(Tests)
endif()

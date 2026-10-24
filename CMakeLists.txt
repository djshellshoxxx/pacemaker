cmake_minimum_required(VERSION 3.22)
project(Pacemaker VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(PACEMAKER_WITH_LINK "Build Ableton Link output (used by later phases)" ON)
option(PACEMAKER_BUILD_TESTS "Build the test harness" ON)
option(PACEMAKER_BUILD_EVAL "Build the offline evaluation CLI" ON)
option(PACEMAKER_BUILD_PLUGIN "Build the JUCE plugin (VST3/CLAP/AU/LV2) and the standalone app" OFF)
option(PACEMAKER_BUILD_CLAP "Build the CLAP plugin (needs PACEMAKER_BUILD_PLUGIN)" ON)
option(PACEMAKER_BUILD_HEADLESS "Build the headless appliance binary (Raspberry Pi)" OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

if(PACEMAKER_BUILD_PLUGIN OR PACEMAKER_BUILD_HEADLESS)
  include(cmake/PacemakerJuce.cmake)
endif()

add_subdirectory(engine)
add_subdirectory(services)
add_subdirectory(web)

if(PACEMAKER_BUILD_EVAL)
  add_subdirectory(tools/eval)
endif()

if(PACEMAKER_BUILD_TESTS)
  enable_testing()
  add_subdirectory(Tests)
endif()

if(PACEMAKER_BUILD_PLUGIN OR PACEMAKER_BUILD_HEADLESS)
  add_subdirectory(juce_support)
endif()

if(PACEMAKER_BUILD_PLUGIN)
  add_subdirectory(plugin)
endif()

if(PACEMAKER_BUILD_HEADLESS)
  add_subdirectory(headless)
endif()

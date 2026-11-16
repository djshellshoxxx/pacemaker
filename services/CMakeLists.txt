add_library(pacemaker_services STATIC
  src/Json.cpp
  src/MidiClock.cpp
  src/Osc.cpp
  src/SyncRenderer.cpp
  src/Calibration.cpp
  src/Settings.cpp
  src/DriftLog.cpp
  src/LinkOutput.cpp
  src/Core.cpp)
target_include_directories(pacemaker_services PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(pacemaker_services PUBLIC pacemaker_engine)
target_compile_features(pacemaker_services PUBLIC cxx_std_17)

if(PACEMAKER_WITH_LINK)
  include(FetchContent)
  FetchContent_Declare(AbletonLink
    GIT_REPOSITORY https://github.com/Ableton/link.git
    GIT_TAG Link-3.1.5
    GIT_SHALLOW TRUE
    GIT_SUBMODULES_RECURSE TRUE)
  FetchContent_GetProperties(AbletonLink)
  if(NOT abletonlink_POPULATED)
    FetchContent_Populate(AbletonLink)
  endif()
  list(APPEND CMAKE_MODULE_PATH ${abletonlink_SOURCE_DIR}/cmake_include)
  include(${abletonlink_SOURCE_DIR}/AbletonLinkConfig.cmake)
  target_link_libraries(pacemaker_services PUBLIC Ableton::Link)
  target_compile_definitions(pacemaker_services PUBLIC PACEMAKER_WITH_LINK=1)
endif()

if(MSVC)
  target_compile_options(pacemaker_services PRIVATE /W4)
  target_link_libraries(pacemaker_services PUBLIC ws2_32 winmm)
else()
  target_compile_options(pacemaker_services PRIVATE -Wall -Wextra)
  find_package(Threads REQUIRED)
  target_link_libraries(pacemaker_services PUBLIC Threads::Threads)
endif()
if(WIN32 AND NOT MSVC)
  target_link_libraries(pacemaker_services PUBLIC ws2_32 winmm)
endif()

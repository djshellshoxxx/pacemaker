add_library(pacemaker_outputs STATIC
  src/Json.cpp src/Settings.cpp src/DriftLog.cpp src/MidiClock.cpp src/Osc.cpp
  src/SyncRenderer.cpp src/Calibration.cpp src/LinkPolicy.cpp src/Http.cpp)
target_include_directories(pacemaker_outputs PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(pacemaker_outputs PUBLIC pacemaker_engine)
find_package(Threads REQUIRED)
target_link_libraries(pacemaker_outputs PUBLIC Threads::Threads)
target_compile_features(pacemaker_outputs PUBLIC cxx_std_17)
if(PACEMAKER_WITH_LINK)
  target_compile_definitions(pacemaker_outputs PUBLIC PACEMAKER_WITH_LINK=1)
endif()
if(NOT MSVC)
  target_compile_options(pacemaker_outputs PRIVATE -Wall -Wextra)
endif()

add_library(pacemaker_engine STATIC
  src/Engine.cpp
  src/FrontEnd.cpp
  src/TempoInduction.cpp
  src/BarTracker.cpp
  src/Tracker.cpp)
target_include_directories(pacemaker_engine
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_features(pacemaker_engine PUBLIC cxx_std_17)
if(MSVC)
  target_compile_options(pacemaker_engine PRIVATE /W4)
  target_compile_definitions(pacemaker_engine PUBLIC _USE_MATH_DEFINES)
else()
  target_compile_options(pacemaker_engine PRIVATE -Wall -Wextra)
endif()

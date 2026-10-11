# pacemaker_core: plain C++17 support code shared by every shell (ES-02, ES-03, RS-01, RS-04).
# No JUCE. Sources are globbed so independent modules can be added without editing this file.
file(GLOB PACEMAKER_CORE_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
add_library(pacemaker_core STATIC ${PACEMAKER_CORE_SOURCES})
target_include_directories(pacemaker_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(pacemaker_core PUBLIC pacemaker_engine)
target_compile_features(pacemaker_core PUBLIC cxx_std_17)
set_target_properties(pacemaker_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
  target_compile_options(pacemaker_core PRIVATE /W4)
else()
  target_compile_options(pacemaker_core PRIVATE -Wall -Wextra)
endif()

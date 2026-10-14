# Fetches JUCE (and clap-juce-extensions when CLAP is requested). Included once from the top-level CMakeLists.
include(FetchContent)
set(JUCE_TAG "8.0.15" CACHE STRING "JUCE git tag to build against")
option(PACEMAKER_SYSTEM_JUCE "Use an installed JUCE (find_package) instead of fetching" OFF)

if(PACEMAKER_SYSTEM_JUCE)
  find_package(JUCE CONFIG REQUIRED)
else()
  FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG ${JUCE_TAG}
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(JUCE)
endif()

if(PACEMAKER_BUILD_CLAP AND PACEMAKER_BUILD_PLUGIN)
  FetchContent_Declare(clap-juce-extensions
    GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
    GIT_TAG 7adee3a1bd4684d4caa5601100e364abccff4b4d
    GIT_SHALLOW TRUE
    GIT_SUBMODULES_RECURSE TRUE)
  FetchContent_MakeAvailable(clap-juce-extensions)
endif()

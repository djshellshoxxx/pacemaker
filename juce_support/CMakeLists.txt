# JUCE glue shared by the standalone, the plugin and the headless build: MIDI port manager, audio device
# bridge and user-settings persistence. No GUI modules.
add_library(pacemaker_juce STATIC
  PortManager.cpp
  AudioBridge.cpp
  UserSettingsStore.cpp)
target_include_directories(pacemaker_juce PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pacemaker_juce
  PUBLIC pacemaker_services
         juce::juce_core juce::juce_events juce::juce_audio_basics juce::juce_audio_devices
  PRIVATE juce::juce_recommended_config_flags)
target_compile_definitions(pacemaker_juce PUBLIC
  JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_MODAL_LOOPS_PERMITTED=1)

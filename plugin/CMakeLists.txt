# Shared GUI (main screen, panels) used by the plugin editor and the standalone window.
add_library(pacemaker_ui STATIC
  ../ui/MainComponent.cpp
  ../ui/panels/PanelStubs.cpp)
target_include_directories(pacemaker_ui PUBLIC ../ui ../ui/panels)
target_link_libraries(pacemaker_ui
  PUBLIC pacemaker_juce juce::juce_gui_basics juce::juce_gui_extra juce::juce_graphics juce::juce_data_structures
  PRIVATE juce::juce_recommended_config_flags)

set(PM_FORMATS VST3)
if(APPLE)
  list(APPEND PM_FORMATS AU)
elseif(UNIX)
  list(APPEND PM_FORMATS LV2)
endif()

juce_add_plugin(PacemakerPlugin
  PRODUCT_NAME "Pacemaker"
  COMPANY_NAME "Pacemaker"
  BUNDLE_ID com.pacemaker.pacemaker
  PLUGIN_MANUFACTURER_CODE Pcmk
  PLUGIN_CODE Pcm1
  FORMATS ${PM_FORMATS}
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT TRUE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS TRUE
  COPY_PLUGIN_AFTER_BUILD FALSE
  VST3_CATEGORIES Fx Analyzer
  AU_MAIN_TYPE kAudioUnitType_Effect
  LV2URI https://github.com/djshellshoxxx/pacemaker
  LV2_SHARED_LIBRARY_NAME pacemaker)

target_sources(PacemakerPlugin PRIVATE PluginProcessor.cpp PluginEditor.cpp)
target_compile_definitions(PacemakerPlugin PUBLIC
  JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_REPORT_APP_USAGE=0)
target_link_libraries(PacemakerPlugin
  PRIVATE pacemaker_ui pacemaker_juce juce::juce_audio_utils juce::juce_audio_processors
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

if(PACEMAKER_BUILD_CLAP)
  clap_juce_extensions_plugin(TARGET PacemakerPlugin
    CLAP_ID "com.pacemaker.pacemaker"
    CLAP_FEATURES audio-effect analyzer utility
    CLAP_MANUAL_URL "https://github.com/djshellshoxxx/pacemaker"
    CLAP_SUPPORT_URL "https://github.com/djshellshoxxx/pacemaker")
endif()

add_subdirectory(../standalone ${CMAKE_CURRENT_BINARY_DIR}/standalone)
if(PACEMAKER_BUILD_TESTS)
  add_subdirectory(tests)
endif()

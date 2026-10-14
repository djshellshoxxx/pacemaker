# Bus layout matrix (A3) and state round trip (A4) against the real processor, no host needed.
juce_add_console_app(PacemakerPluginTests PRODUCT_NAME "PacemakerPluginTests")
target_sources(PacemakerPluginTests PRIVATE PluginTests.cpp ../PluginProcessor.cpp ../PluginEditor.cpp)
target_include_directories(PacemakerPluginTests PRIVATE ..)
target_compile_definitions(PacemakerPluginTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JucePlugin_Build_Standalone=0
  JUCE_STANDALONE_APPLICATION=1)
target_link_libraries(PacemakerPluginTests PRIVATE pacemaker_ui pacemaker_juce juce::juce_audio_utils juce::juce_audio_processors
  juce::juce_recommended_config_flags)
add_test(NAME PacemakerPluginTests COMMAND PacemakerPluginTests)

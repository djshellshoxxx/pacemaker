juce_add_gui_app(PacemakerStandalone
  PRODUCT_NAME "Pacemaker"
  COMPANY_NAME "Pacemaker"
  BUNDLE_ID com.pacemaker.standalone
  VERSION ${PROJECT_VERSION})
target_sources(PacemakerStandalone PRIVATE StandaloneApp.cpp DevicePanel.cpp AutoStart.cpp)
target_compile_definitions(PacemakerStandalone PRIVATE
  JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_APPLICATION_NAME_STRING="Pacemaker" JUCE_APPLICATION_VERSION_STRING="${PROJECT_VERSION}"
  JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_REPORT_APP_USAGE=0)
target_include_directories(PacemakerStandalone PRIVATE ${CMAKE_SOURCE_DIR}/Tests)
target_link_libraries(PacemakerStandalone
  PRIVATE pacemaker_ui pacemaker_juce juce::juce_audio_utils
          juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
if(TARGET pacemaker_web)
  target_link_libraries(PacemakerStandalone PRIVATE pacemaker_web)
  target_compile_definitions(PacemakerStandalone PRIVATE PACEMAKER_HAVE_WEB=1)
endif()

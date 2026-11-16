# pacemaker_headless: the Pacemaker Box service (RS-03 section 2). JUCE console app, no GUI modules.
if(WIN32)
  message(STATUS "pacemaker_headless: not supported on Windows, skipped")
  return()
endif()

if(NOT TARGET pacemaker_web)
  add_subdirectory(${PROJECT_SOURCE_DIR}/web ${PROJECT_BINARY_DIR}/web)
endif()

include(CheckCXXSourceCompiles)
# Status LEDs use the kernel GPIO character device (uAPI v2, the interface libgpiod wraps), so no library is needed.
option(PACEMAKER_HEADLESS_GPIO "Status LEDs on GPIO lines (Linux)" ON)
option(PACEMAKER_HEADLESS_OLED "SSD1306 OLED over /dev/i2c-N (Linux)" ON)
set(PM_GPIO_OK 0)
set(PM_I2C_OK 0)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  if(PACEMAKER_HEADLESS_GPIO)
    check_cxx_source_compiles("#include <linux/gpio.h>\nint main() { return (int) GPIO_V2_GET_LINE_IOCTL; }" PM_HAVE_GPIO_V2)
    if(PM_HAVE_GPIO_V2)
      set(PM_GPIO_OK 1)
    endif()
  endif()
  if(PACEMAKER_HEADLESS_OLED)
    check_cxx_source_compiles("#include <linux/i2c-dev.h>\nint main() { return (int) I2C_SLAVE; }" PM_HAVE_I2C_DEV)
    if(PM_HAVE_I2C_DEV)
      set(PM_I2C_OK 1)
    endif()
  endif()
endif()
message(STATUS "pacemaker_headless: GPIO LEDs=${PM_GPIO_OK}, I2C OLED=${PM_I2C_OK}")

juce_add_console_app(pacemaker_headless
  PRODUCT_NAME "pacemaker_headless"
  VERSION ${PROJECT_VERSION})
target_sources(pacemaker_headless PRIVATE
  src/main.cpp
  src/Config.cpp
  src/Indicators.cpp
  src/Oled.cpp
  src/Offline.cpp)
target_include_directories(pacemaker_headless PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/Tests)                       # DrumMachine.h for --selftest and --bench
target_compile_definitions(pacemaker_headless PRIVATE
  PM_HEADLESS_GPIO=${PM_GPIO_OK}
  PM_HEADLESS_I2C=${PM_I2C_OK}
  PACEMAKER_VERSION_STRING="${PROJECT_VERSION}"
  JUCE_STANDALONE_APPLICATION=1)
target_link_libraries(pacemaker_headless PRIVATE
  pacemaker_juce
  pacemaker_web
  juce::juce_audio_formats
  juce::juce_recommended_config_flags)
if(NOT MSVC)
  target_compile_options(pacemaker_headless PRIVATE -Wall -Wextra)
endif()

# Install layout used by headless/deploy/install.sh (and the Pi image stage).
install(TARGETS pacemaker_headless RUNTIME DESTINATION bin)

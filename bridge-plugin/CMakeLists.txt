cmake_minimum_required(VERSION 3.22)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
project(PSBBridge VERSION 1.0.0)

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)
if(DEFINED ENV{JUCE_DIR} AND EXISTS "$ENV{JUCE_DIR}/CMakeLists.txt")
  add_subdirectory($ENV{JUCE_DIR} JUCE)
else()
  FetchContent_Declare(juce GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.8 GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(juce)
endif()

set(FORMATS VST3)
if(APPLE)
  list(APPEND FORMATS AU)
endif()

juce_add_plugin(PSBBridge
  COMPANY_NAME "Purple SoundBank"
  PLUGIN_MANUFACTURER_CODE PurZ
  PLUGIN_CODE PsbB
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  COPY_PLUGIN_AFTER_BUILD FALSE
  FORMATS ${FORMATS}
  PRODUCT_NAME "PSB Bridge"
  BUNDLE_ID com.purplesoundbank.bridge
  AU_MAIN_TYPE kAudioUnitType_MusicDevice)

target_sources(PSBBridge PRIVATE
  Source/PluginProcessor.cpp
  Source/PluginEditor.cpp
  Source/WsServer.cpp)

target_compile_definitions(PSBBridge PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_DISPLAY_SPLASH_SCREEN=0
  JUCE_REPORT_APP_USAGE=0)

target_link_libraries(PSBBridge PRIVATE
  juce::juce_audio_utils
  juce::juce_audio_processors
  juce::juce_gui_basics
  juce::juce_events
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

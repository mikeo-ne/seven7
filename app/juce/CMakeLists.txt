# ═══════════════════════════════════════════════════════════════════
# seven7 — native JUCE shell (vertical slice)
#
# Builds a Pro Tools-style standalone DAW front-end that drives the
# repository engine kernels (engine/src) in the real audio thread:
#   · s7::engine::fader_db / db_to_gain   (MIX-03 fader taper)
#   · s7::engine::constant_power_pan      (MIX-10 pan law)
#   · s7::engine::accumulate_bus/finalize (ARC-T03 64-bit summing)
#   · s7::domain::TempoMap                (ARC-TIME, GCC/Clang only)
#
# Usage:
#   cmake -B build -S app/juce [-DS7_JUCE_ROOT=/path/to/JUCE]
#   cmake --build build -j
# If S7_JUCE_ROOT is not set, JUCE 8 is fetched automatically (needs network).
# ═══════════════════════════════════════════════════════════════════
cmake_minimum_required(VERSION 3.22)
project(seven7_native VERSION 0.1.0 LANGUAGES CXX)

if(NOT S7_JUCE_ROOT)
  # No local JUCE given → fetch JUCE 8 automatically (needs network on first configure).
  include(FetchContent)
  FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.2
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(JUCE)
else()
  # Use your installed JUCE checkout (any 8.x).
  add_subdirectory(${S7_JUCE_ROOT} ${CMAKE_BINARY_DIR}/juce)
endif()

juce_add_gui_app(seven7
  PRODUCT_NAME "seven7"
  COMPANY_NAME "seven7"
  BUNDLE_ID "com.seven7.daw"
)

target_sources(seven7 PRIVATE
  source/Main.cpp
)

# Engine kernels live at the repo root (header-only).
target_include_directories(seven7 PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../engine/src
)

target_link_libraries(seven7 PRIVATE
  juce::juce_audio_basics
  juce::juce_audio_devices
  juce::juce_audio_formats
  juce::juce_gui_basics
  juce::juce_gui_extra
)

set_target_properties(seven7 PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
)

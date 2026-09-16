# seven7 — native desktop application (JUCE 9 host for the engine + web UI)
#
#   cmake -B build-app -S app -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-app --target seven7
#
# The UI (ui/dist) is built by CMake via npm when Node is available and embedded
# as binary data. Pass -DS7_UI_DEV_SERVER=http://localhost:5174 to load the Vite
# dev server instead (hot reload) — see docs/07-ui-bridge-protocol.md §4.

cmake_minimum_required(VERSION 3.22)
project(seven7App VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(APPLE)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")
endif()

option(S7_BUILD_UI "Run `npm ci && npm run build` in ui/ to refresh the embedded UI" ON)
option(S7_REQUIRE_UI "Fail the configure step if ui/dist is empty (CI guard against shipping the fallback page)" OFF)
set(S7_UI_DEV_SERVER "" CACHE STRING "If set, the app loads this URL (Vite dev server) instead of the embedded UI")
set(JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout (skips FetchContent)")

# ─── JUCE ────────────────────────────────────────────────────────────────────
if(JUCE_DIR)
  add_subdirectory(${JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)
else()
  include(FetchContent)
  FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        9.0.2
    GIT_SHALLOW    TRUE)
  FetchContent_MakeAvailable(JUCE)
endif()

# ─── engine core (header-mostly C++20 library from ../engine) ────────────────
set(S7_ENGINE_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../engine/src)
add_library(s7core_app STATIC
  ${S7_ENGINE_SRC}/engine/dsp_graph.cpp
  ${S7_ENGINE_SRC}/engine/session.cpp
  ${S7_ENGINE_SRC}/app/controller.cpp)
target_include_directories(s7core_app PUBLIC ${S7_ENGINE_SRC})
target_compile_features(s7core_app PUBLIC cxx_std_20)
if(MSVC)
  target_compile_options(s7core_app PRIVATE /W4)
else()
  target_compile_options(s7core_app PRIVATE -Wall -Wextra)
endif()

# ─── UI bundle → binary data ─────────────────────────────────────────────────
set(S7_UI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../ui)
set(S7_UI_DIST ${S7_UI_DIR}/dist)
find_program(NPM_EXECUTABLE npm)
if(S7_BUILD_UI AND NPM_EXECUTABLE)
  message(STATUS "seven7: building UI with ${NPM_EXECUTABLE} in ${S7_UI_DIR}")
  execute_process(COMMAND ${NPM_EXECUTABLE} install --no-audit --no-fund WORKING_DIRECTORY ${S7_UI_DIR} RESULT_VARIABLE npm_install_rc)
  if(NOT npm_install_rc EQUAL 0)
    message(WARNING "seven7: npm install failed (${npm_install_rc}); using existing ui/dist if present")
  else()
    execute_process(COMMAND ${NPM_EXECUTABLE} run build WORKING_DIRECTORY ${S7_UI_DIR} RESULT_VARIABLE npm_build_rc)
    if(NOT npm_build_rc EQUAL 0)
      message(WARNING "seven7: npm run build failed (${npm_build_rc}); using existing ui/dist if present")
    endif()
  endif()
endif()

file(GLOB_RECURSE S7_UI_FILES CONFIGURE_DEPENDS ${S7_UI_DIST}/*)
if(NOT S7_UI_FILES)
  if(S7_REQUIRE_UI)
    message(FATAL_ERROR "seven7: ui/dist is empty and S7_REQUIRE_UI is ON — the UI build failed (see npm output above).")
  endif()
  message(WARNING "seven7: ui/dist is empty — the app will show a fallback page. Run `npm run build` in ui/ or set S7_UI_DEV_SERVER.")
  set(S7_UI_FILES ${CMAKE_CURRENT_SOURCE_DIR}/resources/fallback.html)
else()
  list(LENGTH S7_UI_FILES _s7_ui_count)
  set(_s7_ui_names "")
  foreach(_f ${S7_UI_FILES})
    get_filename_component(_n ${_f} NAME)
    string(APPEND _s7_ui_names " ${_n}")
  endforeach()
  message(STATUS "seven7: embedding ${_s7_ui_count} UI files:${_s7_ui_names}")
  if(NOT EXISTS ${S7_UI_DIST}/index.html)
    message(FATAL_ERROR "seven7: ui/dist has no index.html — refusing to embed a broken bundle")
  endif()
endif()
juce_add_binary_data(s7ui_data HEADER_NAME S7UIData.h NAMESPACE S7UIData SOURCES ${S7_UI_FILES})

# ─── the app ─────────────────────────────────────────────────────────────────
juce_add_gui_app(seven7
  PRODUCT_NAME "seven7"
  COMPANY_NAME "seven7"
  BUNDLE_ID "app.seven7.desktop"
  VERSION ${PROJECT_VERSION}
  NEEDS_WEB_BROWSER TRUE
  NEEDS_CURL FALSE
  MICROPHONE_PERMISSION_ENABLED TRUE
  MICROPHONE_PERMISSION_TEXT "seven7 needs the microphone / audio interface inputs to record audio tracks."
  DOCUMENT_EXTENSIONS s7proj
  HARDENED_RUNTIME_ENABLED TRUE
  HARDENED_RUNTIME_OPTIONS com.apple.security.device.audio-input)

target_sources(seven7 PRIVATE
  src/Main.cpp
  src/AudioHost.cpp
  src/WebShell.cpp)

target_compile_definitions(seven7 PRIVATE
  JUCE_WEB_BROWSER=1
  JUCE_USE_CURL=0
  JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:seven7,JUCE_PRODUCT_NAME>"
  JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:seven7,JUCE_VERSION>"
  JUCE_DISPLAY_SPLASH_SCREEN=0
  JUCE_MODAL_LOOPS_PERMITTED=0
  S7_UI_DEV_SERVER="${S7_UI_DEV_SERVER}")

target_link_libraries(seven7 PRIVATE
  s7core_app
  s7ui_data
  juce::juce_audio_basics
  juce::juce_audio_devices
  juce::juce_audio_formats
  juce::juce_audio_utils
  juce::juce_core
  juce::juce_data_structures
  juce::juce_events
  juce::juce_graphics
  juce::juce_gui_basics
  juce::juce_gui_extra
  PUBLIC
  juce::juce_recommended_config_flags
  juce::juce_recommended_lto_flags
  juce::juce_recommended_warning_flags)

# seven7 engine — build (spec: docs/01 ARC-T06)
cmake_minimum_required(VERSION 3.20)
project(seven7_engine VERSION 0.9.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -Werror)
endif()

find_package(Threads REQUIRED)

# --- Engine core library -----------------------------------------------------
# Header-only units: domain/time_model.h, domain/project.h, domain/edit_engine.h,
# engine/lane_scheduler.h, engine/mix_math.h, engine/rt_primitives.h, engine/synth.h,
# pal/audio_device.h, pal/wav_file.h, pal/json.h, pal/project_codec.h
add_library(s7core STATIC
  src/engine/dsp_graph.cpp
  src/engine/session.cpp
  src/app/controller.cpp
)
target_include_directories(s7core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(s7core PUBLIC Threads::Threads)
add_library(s7::core ALIAS s7core)

# --- Headless engine binary (CI conformance backstop) ------------------------
add_executable(s7engine apps/s7engine/main.cpp)
target_link_libraries(s7engine PRIVATE s7core)

# --- Development bridge: Controller over HTTP for the browser-hosted UI -------
# POSIX sockets only (Linux/macOS). The desktop app (apps/seven7/) embeds the
# same Controller behind JUCE's WebBrowserComponent instead.
if(UNIX)
  add_executable(s7bridge apps/s7bridge/main.cpp)
  target_link_libraries(s7bridge PRIVATE s7core)
endif()

# --- Tests (CTest) -----------------------------------------------------------
if(NOT DEFINED S7_BUILD_TESTS)
  set(S7_BUILD_TESTS ON)
endif()
if(S7_BUILD_TESTS)
  enable_testing()
  set(S7_TEST_SUITES time_model dsp_graph mix_math lane_scheduler edit_engine session codecs controller)
  foreach(suite IN LISTS S7_TEST_SUITES)
    add_executable(test_${suite} tests/test_${suite}.cpp)
    target_link_libraries(test_${suite} PRIVATE s7core)
    target_include_directories(test_${suite} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    add_test(NAME ${suite} COMMAND test_${suite})
  endforeach()
endif()

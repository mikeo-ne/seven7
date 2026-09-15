# seven7 engine — build (spec: docs/01 ARC-T06)
cmake_minimum_required(VERSION 3.20)
project(seven7_engine VERSION 0.9.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -Werror)
endif()

# --- Engine core library -----------------------------------------------------
# Header-only units: domain/time_model.h, engine/lane_scheduler.h,
# engine/mix_math.h, pal/audio_device.h
add_library(s7core STATIC
  src/engine/dsp_graph.cpp
)
target_include_directories(s7core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# --- Headless engine binary (CI conformance backstop) ------------------------
add_executable(s7engine apps/s7engine/main.cpp)
target_link_libraries(s7engine PRIVATE s7core)

# --- Tests (CTest) -----------------------------------------------------------
enable_testing()
set(S7_TEST_SUITES time_model dsp_graph mix_math lane_scheduler)
foreach(suite IN LISTS S7_TEST_SUITES)
  add_executable(test_${suite} tests/test_${suite}.cpp)
  target_link_libraries(test_${suite} PRIVATE s7core)
  target_include_directories(test_${suite} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
  add_test(NAME ${suite} COMMAND test_${suite})
endforeach()

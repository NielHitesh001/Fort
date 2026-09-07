cmake_minimum_required(VERSION 3.20)
project(luv_flicker LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

option(LUV_ENABLE_ASAN_UBSAN
    "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(LUV_ENABLE_TSAN
    "Enable ThreadSanitizer" OFF)
option(LUV_ENABLE_LONG_STRESS_TESTS
    "Include the 10-million-message stress test in CTest" ON)

add_library(luv_core INTERFACE)
target_include_directories(luv_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(luv_core INTERFACE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic>)
if(LUV_ENABLE_ASAN_UBSAN)
    target_compile_options(luv_core INTERFACE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-fsanitize=address,undefined;-fno-omit-frame-pointer>)
    target_link_options(luv_core INTERFACE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-fsanitize=address,undefined>)
endif()
if(LUV_ENABLE_TSAN)
    target_compile_options(luv_core INTERFACE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-fsanitize=thread;-fno-omit-frame-pointer>)
    target_link_options(luv_core INTERFACE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-fsanitize=thread>)
endif()

set(LUV_TESTS
    test_ai_telemetry
    test_arena
    test_execution
    test_feed
    test_lob
    test_stress
    test_decoder_fuzz
    test_concurrency
    test_ouch_parser
    test_risk_validator
    test_telemetry_export
    test_prometheus_export
    test_shutdown_controller
    test_strategy
    test_crash_recovery
    test_crash_recovery_process
    test_audit_integrity
    test_day4_integration
)

foreach(test_name IN LISTS LUV_TESTS)
    add_executable(${test_name} ${test_name}.cpp)
    target_link_libraries(${test_name} PRIVATE luv_core Threads::Threads)
    target_compile_options(${test_name} PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-UNDEBUG>)
endforeach()

add_executable(test_latency test_latency.cpp)
target_link_libraries(test_latency PRIVATE luv_core Threads::Threads)
target_compile_options(test_latency PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-UNDEBUG>)

add_executable(staging_runner staging_runner.cpp)
target_link_libraries(staging_runner PRIVATE luv_core Threads::Threads)
target_compile_options(staging_runner PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-UNDEBUG>)

add_executable(staging_soak_runner staging_soak_runner.cpp)
target_link_libraries(staging_soak_runner PRIVATE luv_core Threads::Threads)
target_compile_options(staging_soak_runner PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-UNDEBUG>)

add_executable(crash_recovery_harness crash_recovery_harness.cpp)
target_link_libraries(crash_recovery_harness PRIVATE luv_core Threads::Threads)
target_compile_options(crash_recovery_harness PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-UNDEBUG>)

add_executable(main_engine main_engine.cpp)
target_link_libraries(main_engine PRIVATE luv_core Threads::Threads)
target_compile_options(main_engine PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-UNDEBUG>)

enable_testing()
foreach(test_name IN LISTS LUV_TESTS)
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()
add_test(NAME crash_recovery_harness COMMAND crash_recovery_harness)

if(NOT LUV_ENABLE_LONG_STRESS_TESTS)
    set_tests_properties(test_stress PROPERTIES DISABLED TRUE)
endif()

cmake_minimum_required(VERSION 3.20)
project(luv_flicker LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(LUV_ENABLE_ASAN_UBSAN
    "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(LUV_ENABLE_TSAN
    "Enable ThreadSanitizer" OFF)
option(LUV_ENABLE_LONG_STRESS_TESTS
    "Include the long stress test in CTest" OFF)
set(LUV_STRESS_MESSAGES 250000 CACHE STRING
    "Number of messages used by the CTest stress target")
option(LUV_ENABLE_DYNAMIC_MODEL_LOADING
    "Allow loading trusted Treelite/TL2cgen shared-object predictors" OFF)
option(LUV_REQUIRE_MLOCK
    "Fail Arena initialisation when infrastructure memory cannot be locked" OFF)
option(LUV_ENABLE_DPDK
    "Build the Linux DPDK packet I/O backend" OFF)

find_package(Threads REQUIRED)

add_library(packet_io STATIC)
if(APPLE OR NOT LUV_ENABLE_DPDK)
    target_sources(packet_io PRIVATE packet_io_stub.c)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(DPDK REQUIRED IMPORTED_TARGET libdpdk)
    target_sources(packet_io PRIVATE packet_io_dpdk.c)
    target_link_libraries(packet_io PRIVATE PkgConfig::DPDK)
endif()
target_include_directories(packet_io PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_library(luv_core INTERFACE)
target_include_directories(luv_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(luv_core INTERFACE packet_io)
target_compile_options(luv_core INTERFACE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-O3;-Wall;-Wextra;-Wpedantic>)
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
if(LUV_ENABLE_DYNAMIC_MODEL_LOADING)
    target_compile_definitions(luv_core INTERFACE LUV_ENABLE_DYNAMIC_MODEL_LOADING=1)
endif()
if(LUV_REQUIRE_MLOCK)
    target_compile_definitions(luv_core INTERFACE LUV_REQUIRE_MLOCK=1)
endif()

add_executable(luv_engine main_engine.cpp)
target_link_libraries(luv_engine PRIVATE luv_core Threads::Threads)
if(CMAKE_DL_LIBS)
    target_link_libraries(luv_engine PRIVATE ${CMAKE_DL_LIBS})
endif()

function(luv_add_test test_name)
    add_executable(luv_${test_name} test_${test_name}.cpp)
    target_link_libraries(luv_${test_name} PRIVATE luv_core Threads::Threads)
    target_compile_options(luv_${test_name} PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-UNDEBUG>)
    if(CMAKE_DL_LIBS)
        target_link_libraries(luv_${test_name} PRIVATE ${CMAKE_DL_LIBS})
    endif()
    if(test_name STREQUAL "stress")
        add_test(NAME ${test_name} COMMAND luv_${test_name} ${LUV_STRESS_MESSAGES})
    else()
        add_test(NAME ${test_name} COMMAND luv_${test_name})
    endif()
endfunction()

set(LUV_TESTS
    ai_telemetry
    arena
    audit_integrity
    concurrency
    crash_recovery
    crash_recovery_process
    day4_integration
    decoder_fuzz
    execution
    feed
    lob
    moldudp64_fixture
    ouch_parser
    prometheus_export
    risk_validator
    sequence_tracker
    shutdown_controller
    strategy
    stress
    telemetry_export
)

enable_testing()
foreach(test_name IN LISTS LUV_TESTS)
    luv_add_test(${test_name})
endforeach()

add_executable(luv_latency_benchmark test_latency.cpp)
target_link_libraries(luv_latency_benchmark PRIVATE luv_core Threads::Threads)
target_compile_options(luv_latency_benchmark PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-UNDEBUG>)

add_executable(luv_staging_runner staging_runner.cpp)
target_link_libraries(luv_staging_runner PRIVATE luv_core Threads::Threads)

add_executable(luv_staging_soak_runner staging_soak_runner.cpp)
target_link_libraries(luv_staging_soak_runner PRIVATE luv_core Threads::Threads)

add_executable(luv_crash_recovery_harness crash_recovery_harness.cpp)
target_link_libraries(luv_crash_recovery_harness PRIVATE luv_core Threads::Threads)
add_test(NAME crash_recovery_harness COMMAND luv_crash_recovery_harness)

if(NOT LUV_ENABLE_LONG_STRESS_TESTS)
    set_tests_properties(stress PROPERTIES DISABLED TRUE)
else()
    set_tests_properties(stress PROPERTIES
        PASS_REGULAR_EXPRESSION "Stress test passed\\.")
endif()

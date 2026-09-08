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
find_package(OpenSSL REQUIRED)

if(LUV_ENABLE_DPDK)
    if(APPLE)
        message(FATAL_ERROR "LUV_ENABLE_DPDK requires a Linux host; the stub packet I/O backend is used on macOS.")
    endif()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(DPDK REQUIRED IMPORTED_TARGET libdpdk)
endif()

add_library(packet_io STATIC)
if(APPLE OR NOT LUV_ENABLE_DPDK)
    target_sources(packet_io PRIVATE packet_io_stub.c)
else()
    target_sources(packet_io PRIVATE packet_io_dpdk.c)
    target_link_libraries(packet_io PRIVATE PkgConfig::DPDK)
endif()
target_include_directories(packet_io PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_library(luv_core INTERFACE)
target_include_directories(luv_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(luv_core INTERFACE packet_io OpenSSL::Crypto)
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
    10b18
    advanced_circuit_breaker
    ai_telemetry
    algo_execution
    allocation
    amm_liquidity
    arena
    as_mm
    ats_n
    auction
    audit_integrity
    backtest
    bars
    best_execution_rts27
    bond_analytics
    cat_reporting
    cds
    chaos_feed
    clearing
    clock_sync
    collar
    collateral
    compliance_aml
    concurrency
    contingent_orders
    corporate_actions
    crash_recovery
    crash_recovery_process
    cross_asset_margin
    dark_pool
    day4_integration
    decoder_fuzz
    determinism_fuzz
    diagnostics_and_multileg
    disaster_recovery
    drop_copy
    dtc_settlement
    etf_arb
    execution
    factor_stress
    feed
    financing
    fix_protocol
    form_13f
    form_pf
    fx_forward
    fx_triangulation
    greeks
    hft_feature_pipeline
    hw_watchdog
    irs
    iv_solver
    lob
    lob_sanity
    locate
    luld
    margin
    margin_call_watchdog
    market_access_15c3_5
    mbo
    mempool
    moldudp64_fixture
    multi_feed_and_compliance
    net_capital_15c3_1
    net_ring
    options_settlement
    order_peg
    order_types
    ouch_parser
    perps_fair_value
    perps_funding
    pnl_attribution
    precision
    prometheus_export
    raft_sync
    rebates
    reconciliation
    reg_m
    reg_nms
    reg_sho_204
    regime_switching_mm
    risk_engine
    risk_validator
    rule_13h
    rule_605
    rule_606
    sequence_tracker
    sharding
    shutdown_controller
    sor_and_gateway
    sor_sim
    sor_smart_router
    span_margin
    stp_mmp
    strategy
    strategy_sandbox
    stress
    surveillance
    telemetry_export
    tick_rule
    var
    vol_surface
    vpin
    worm_audit
    xccy_swap
    yield_curve
)

if(NOT LUV_ENABLE_DPDK)
    list(APPEND LUV_TESTS packet_io)
endif()

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

if(APPLE)
    add_executable(luv_staging_soak_runner staging_soak_runner.cpp)
    target_link_libraries(luv_staging_soak_runner PRIVATE luv_core Threads::Threads)
endif()

add_executable(luv_crash_recovery_harness crash_recovery_harness.cpp)
target_link_libraries(luv_crash_recovery_harness PRIVATE luv_core Threads::Threads)
add_test(NAME crash_recovery_harness COMMAND luv_crash_recovery_harness)

if(NOT LUV_ENABLE_LONG_STRESS_TESTS)
    set_tests_properties(stress PROPERTIES DISABLED TRUE)
else()
    set_tests_properties(stress PROPERTIES
        PASS_REGULAR_EXPRESSION "Stress test passed\\.")
endif()

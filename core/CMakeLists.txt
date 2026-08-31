option(ALPHA_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

add_library(yyjson STATIC ${PROJECT_SOURCE_DIR}/third_party/yyjson/yyjson.c)
target_include_directories(yyjson PUBLIC ${PROJECT_SOURCE_DIR}/third_party/yyjson)
set_target_properties(yyjson PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(
    alpha_core STATIC
    src/alpha.c
    src/domain/date.c
    src/domain/round.c
    src/backtest/cost_model.c
    src/backtest/signal_source.c
    src/backtest/metrics.c
    src/backtest/engine.c
    src/market_data/market_data.c
    src/portfolio/blending.c
    src/portfolio/sizing.c
    src/risk/gates.c
    src/indicators/screener.c
)
add_library(alpha::core ALIAS alpha_core)
target_include_directories(
    alpha_core
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(alpha_core PUBLIC yyjson)
if(UNIX)
    target_link_libraries(alpha_core PUBLIC m)
endif()
target_compile_options(alpha_core PRIVATE -Wall -Wextra -Werror -ffp-contract=off)
set_target_properties(alpha_core PROPERTIES OUTPUT_NAME alpha_core POSITION_INDEPENDENT_CODE ON)

# Shared fixture loader used by the benchmark runner and the parity test.
add_library(alpha_appsupport STATIC apps/backtest_fixture.c)
target_include_directories(alpha_appsupport PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/apps)
target_link_libraries(alpha_appsupport PUBLIC alpha::core yyjson)
target_compile_options(alpha_appsupport PRIVATE -Wall -Wextra -Werror -ffp-contract=off)

if(ALPHA_ENABLE_SANITIZERS)
    target_compile_options(alpha_core PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(alpha_core PUBLIC -fsanitize=address,undefined)
    target_compile_options(alpha_appsupport PRIVATE -fsanitize=address,undefined
                           -fno-omit-frame-pointer)
endif()

# Benchmark / inspection runners.
add_executable(backtest_runner apps/backtest_runner.c)
target_link_libraries(backtest_runner PRIVATE alpha_appsupport)
target_compile_options(backtest_runner PRIVATE -Wall -Wextra -Werror -ffp-contract=off)
# Expose POSIX clock_gettime + BSD getrusage/ru_maxrss under -std=c11 on glibc.
target_compile_definitions(backtest_runner PRIVATE _DEFAULT_SOURCE)

add_executable(blend_runner apps/blend_runner.c)
target_link_libraries(blend_runner PRIVATE alpha::core)
target_compile_options(blend_runner PRIVATE -Wall -Wextra -Werror -ffp-contract=off)
target_compile_definitions(blend_runner PRIVATE _DEFAULT_SOURCE)
add_executable(risk_runner apps/risk_runner.c)
target_link_libraries(risk_runner PRIVATE alpha::core)
target_compile_options(risk_runner PRIVATE -Wall -Wextra -Werror -ffp-contract=off)
target_compile_definitions(risk_runner PRIVATE _DEFAULT_SOURCE)
if(ALPHA_ENABLE_SANITIZERS)
    target_compile_options(backtest_runner PRIVATE -fsanitize=address,undefined
                           -fno-omit-frame-pointer)
    target_link_options(backtest_runner PRIVATE -fsanitize=address,undefined)
    target_compile_options(blend_runner PRIVATE -fsanitize=address,undefined
                           -fno-omit-frame-pointer)
    target_link_options(blend_runner PRIVATE -fsanitize=address,undefined)
    target_compile_options(risk_runner PRIVATE -fsanitize=address,undefined
                           -fno-omit-frame-pointer)
    target_link_options(risk_runner PRIVATE -fsanitize=address,undefined)
endif()

add_library(unity STATIC ${PROJECT_SOURCE_DIR}/third_party/unity/unity.c)
target_include_directories(unity PUBLIC ${PROJECT_SOURCE_DIR}/third_party/unity)
target_compile_definitions(unity PUBLIC UNITY_INCLUDE_DOUBLE)

# Register a Unity test executable linked against the core (+ optional appsupport).
function(alpha_add_test name)
    add_executable(${name} tests/${name}.c)
    target_link_libraries(${name} PRIVATE alpha::core unity ${ARGN})
    target_compile_options(${name} PRIVATE -Wall -Wextra -Werror -ffp-contract=off)
    if(ALPHA_ENABLE_SANITIZERS)
        target_compile_options(${name} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${name} PRIVATE -fsanitize=address,undefined)
    endif()
endfunction()

alpha_add_test(test_scaffold)
add_test(NAME scaffold COMMAND test_scaffold)

alpha_add_test(test_cost_model)
add_test(NAME cost_model COMMAND test_cost_model)

alpha_add_test(test_metrics)
add_test(NAME metrics COMMAND test_metrics)

alpha_add_test(test_engine)
add_test(NAME engine COMMAND test_engine)

alpha_add_test(test_parity_backtest alpha_appsupport)
# Golden/fixture paths are relative to the repository root.
add_test(NAME parity_backtest COMMAND test_parity_backtest)
set_tests_properties(parity_backtest PROPERTIES WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})

alpha_add_test(test_market_data)
add_test(NAME market_data COMMAND test_market_data)
set_tests_properties(market_data PROPERTIES WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})

alpha_add_test(test_blending)
add_test(NAME blending COMMAND test_blending)
set_tests_properties(blending PROPERTIES WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})

alpha_add_test(test_risk)
add_test(NAME risk COMMAND test_risk)
set_tests_properties(risk PROPERTIES WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})

alpha_add_test(test_screener)
add_test(NAME screener COMMAND test_screener)
set_tests_properties(screener PROPERTIES WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})

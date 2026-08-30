option(ALPHA_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

add_library(yyjson STATIC ${PROJECT_SOURCE_DIR}/third_party/yyjson/yyjson.c)
target_include_directories(yyjson PUBLIC ${PROJECT_SOURCE_DIR}/third_party/yyjson)
set_target_properties(yyjson PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(alpha_core STATIC src/alpha.c)
add_library(alpha::core ALIAS alpha_core)
target_include_directories(
    alpha_core
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(alpha_core PUBLIC yyjson)
target_compile_options(alpha_core PRIVATE -Wall -Wextra -Werror -ffp-contract=off)
set_target_properties(alpha_core PROPERTIES OUTPUT_NAME alpha_core POSITION_INDEPENDENT_CODE ON)

if(ALPHA_ENABLE_SANITIZERS)
    target_compile_options(alpha_core PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(alpha_core PUBLIC -fsanitize=address,undefined)
endif()

add_library(unity STATIC ${PROJECT_SOURCE_DIR}/third_party/unity/unity.c)
target_include_directories(unity PUBLIC ${PROJECT_SOURCE_DIR}/third_party/unity)
target_compile_definitions(unity PUBLIC UNITY_INCLUDE_DOUBLE)

add_executable(test_scaffold tests/test_scaffold.c)
target_link_libraries(test_scaffold PRIVATE alpha::core unity)
target_compile_options(test_scaffold PRIVATE -Wall -Wextra -Werror -ffp-contract=off)
if(ALPHA_ENABLE_SANITIZERS)
    target_compile_options(test_scaffold PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(test_scaffold PRIVATE -fsanitize=address,undefined)
endif()
add_test(NAME scaffold COMMAND test_scaffold)

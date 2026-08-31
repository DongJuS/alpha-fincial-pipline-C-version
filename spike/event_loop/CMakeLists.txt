cmake_minimum_required(VERSION 3.20)
project(alpha_event_loop_spike C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
if(NOT DEFINED ALPHA_LWS_PREFIX)
    message(FATAL_ERROR
        "ALPHA_LWS_PREFIX must point to the verified pinned libwebsockets stage")
endif()
set(_alpha_lws_pc_path "${ALPHA_LWS_PREFIX}/lib/pkgconfig")
if(EXISTS "${ALPHA_LWS_PREFIX}/lib64/pkgconfig")
    set(_alpha_lws_pc_path "${ALPHA_LWS_PREFIX}/lib64/pkgconfig")
endif()
if(NOT EXISTS "${_alpha_lws_pc_path}/libwebsockets.pc")
    message(FATAL_ERROR "Pinned libwebsockets pkg-config metadata is missing")
endif()
set(ENV{PKG_CONFIG_PATH}
    "${_alpha_lws_pc_path}:$ENV{PKG_CONFIG_PATH}")
pkg_check_modules(LWS REQUIRED IMPORTED_TARGET libwebsockets)
if(NOT LWS_VERSION STREQUAL "4.3.3-v4.3.3")
    message(FATAL_ERROR "Unexpected pinned libwebsockets version: ${LWS_VERSION}")
endif()
pkg_check_modules(PQ REQUIRED IMPORTED_TARGET libpq>=14)
pkg_check_modules(HIREDIS REQUIRED IMPORTED_TARGET hiredis>=1.0)
pkg_check_modules(CURL REQUIRED IMPORTED_TARGET libcurl>=7.80)

add_executable(alpha_event_loop_spike main.c)
target_compile_options(alpha_event_loop_spike PRIVATE -Wall -Wextra -Werror)
target_link_libraries(alpha_event_loop_spike PRIVATE
    PkgConfig::LWS PkgConfig::PQ PkgConfig::HIREDIS PkgConfig::CURL)

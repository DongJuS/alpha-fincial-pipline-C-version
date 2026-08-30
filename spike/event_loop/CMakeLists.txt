cmake_minimum_required(VERSION 3.20)
project(alpha_event_loop_spike C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LWS REQUIRED IMPORTED_TARGET libwebsockets>=4.3)
pkg_check_modules(PQ REQUIRED IMPORTED_TARGET libpq>=14)
pkg_check_modules(HIREDIS REQUIRED IMPORTED_TARGET hiredis>=1.0)
pkg_check_modules(CURL REQUIRED IMPORTED_TARGET libcurl>=7.80)

add_executable(alpha_event_loop_spike main.c)
target_compile_options(alpha_event_loop_spike PRIVATE -Wall -Wextra -Werror)
target_link_libraries(alpha_event_loop_spike PRIVATE
    PkgConfig::LWS PkgConfig::PQ PkgConfig::HIREDIS PkgConfig::CURL)

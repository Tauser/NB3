# Host tests for the pure C17 cores in main/core. Built with the host
# compiler, no ESP-IDF involved:
#   cmake -S tests -B build-host-tests
#   cmake --build build-host-tests
#   ctest --test-dir build-host-tests --output-on-failure
cmake_minimum_required(VERSION 3.16)
project(nb3_host_tests C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
add_compile_options(-Wall -Wextra -Werror -pedantic)

set(CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../main/core)

enable_testing()

function(nb_host_test name)
    add_executable(${name} ${name}.c ${ARGN})
    target_include_directories(${name} PRIVATE ${CORE_DIR} ${CMAKE_CURRENT_SOURCE_DIR})
    if(NOT MSVC)
        target_link_libraries(${name} PRIVATE m)
    endif()
    add_test(NAME ${name} COMMAND ${name})
endfunction()

nb_host_test(test_expression ${CORE_DIR}/nb_expression.c)
nb_host_test(test_arbiter ${CORE_DIR}/nb_arbiter.c ${CORE_DIR}/nb_expression.c)
nb_host_test(test_touch ${CORE_DIR}/nb_touch.c)
nb_host_test(test_avatar ${CORE_DIR}/nb_avatar.c)
nb_host_test(test_mood ${CORE_DIR}/nb_mood.c)

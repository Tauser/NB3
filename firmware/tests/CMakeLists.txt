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
nb_host_test(test_gaze ${CORE_DIR}/nb_gaze.c)
nb_host_test(test_initiative ${CORE_DIR}/nb_initiative.c)
nb_host_test(test_energy ${CORE_DIR}/nb_energy.c ${CORE_DIR}/nb_initiative.c)
nb_host_test(test_arc ${CORE_DIR}/nb_arc.c ${CORE_DIR}/nb_avatar.c ${CORE_DIR}/nb_expression.c)
nb_host_test(test_face_link ${CORE_DIR}/nb_face_link.c ${CORE_DIR}/nb_avatar.c
             ${CORE_DIR}/nb_arbiter.c ${CORE_DIR}/nb_expression.c)
nb_host_test(test_mood ${CORE_DIR}/nb_mood.c)
nb_host_test(test_affect ${CORE_DIR}/nb_affect.c ${CORE_DIR}/nb_expression.c)
nb_host_test(test_overlay ${CORE_DIR}/nb_overlay.c)
nb_host_test(test_clock ${CORE_DIR}/nb_clock.c)
nb_host_test(test_store ${CORE_DIR}/nb_store.c)
nb_host_test(test_json ${CORE_DIR}/nb_json.c)
nb_host_test(test_memory ${CORE_DIR}/nb_memory.c ${CORE_DIR}/nb_json.c ${CORE_DIR}/nb_clock.c
             ${CORE_DIR}/nb_store.c)
nb_host_test(test_head_gesture ${CORE_DIR}/nb_head_gesture.c)
nb_host_test(test_drift ${CORE_DIR}/nb_drift.c ${CORE_DIR}/nb_memory.c ${CORE_DIR}/nb_json.c
             ${CORE_DIR}/nb_clock.c ${CORE_DIR}/nb_personality.c ${CORE_DIR}/nb_affect.c
             ${CORE_DIR}/nb_expression.c)
nb_host_test(test_personality ${CORE_DIR}/nb_personality.c ${CORE_DIR}/nb_affect.c
             ${CORE_DIR}/nb_expression.c)
nb_host_test(test_behavior ${CORE_DIR}/nb_behavior.c ${CORE_DIR}/nb_arbiter.c
             ${CORE_DIR}/nb_expression.c)
nb_host_test(test_scs ${CORE_DIR}/nb_scs.c)
nb_host_test(test_motion_safety ${CORE_DIR}/nb_motion_safety.c)
nb_host_test(test_banner ${CORE_DIR}/nb_banner.c)
nb_host_test(test_hpf ${CORE_DIR}/nb_hpf.c)

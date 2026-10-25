# NoiseBot v3 firmware. The project is ours: main/ boots the body first and
# starts xiaozhi (fetched at a fixed tag by fetch_repos.py) as the AI mind.
cmake_minimum_required(VERSION 3.16)

set(XIAOZHI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/xiaozhi-esp32")
if(NOT EXISTS "${XIAOZHI_DIR}/main/application.cc")
    message(FATAL_ERROR "xiaozhi-esp32 not found: run 'python fetch_repos.py' first")
endif()

# xiaozhi's defaults first, NB3 overrides last. ESP-IDF also loads the
# matching sdkconfig.defaults.<target> next to each listed file.
set(SDKCONFIG_DEFAULTS
    "${XIAOZHI_DIR}/sdkconfig.defaults;${CMAKE_CURRENT_SOURCE_DIR}/sdkconfig.defaults")

add_compile_options(-Wno-missing-field-initializers)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# Build only main and what it depends on, as xiaozhi does.
idf_build_set_property(MINIMAL_BUILD ON)

set(PROJECT_VER "0.1.0")
project(noisebot-v3)

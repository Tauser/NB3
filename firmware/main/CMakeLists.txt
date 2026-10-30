# main = NB3 code + the xiaozhi sources it uses. xiaozhi's own main/CMakeLists
# is not used: this file lists what NB3 needs from it (ESP32-S3 only) and
# reproduces the generation steps those sources rely on. When the xiaozhi tag
# changes, diff its main/CMakeLists.txt against this file.

set(XIAOZHI_MAIN "${CMAKE_CURRENT_SOURCE_DIR}/../xiaozhi-esp32/main")
set(XIAOZHI_SCRIPTS "${CMAKE_CURRENT_SOURCE_DIR}/../xiaozhi-esp32/scripts")

# --- NB3 ---------------------------------------------------------------------
file(GLOB_RECURSE NB3_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)
set(NB3_INCLUDE_DIRS ".")

set(BOARD_TYPE "noisebot-v3")
set(BOARD_NAME "noisebot-v3")
set(BOARD_MANUFACTURER "")
set(BUILTIN_TEXT_FONT font_noto_sans_basic_16_4)
set(BUILTIN_ICON_FONT font_material_symbols_16_4)
set(DEFAULT_EMOJI_COLLECTION noto-color-emoji_32)

# --- xiaozhi sources (mirrors xiaozhi main/CMakeLists.txt, ESP32-S3 subset) ---
set(XIAOZHI_SOURCES
    "audio/audio_codec.cc"
    "audio/audio_debugger.cc"
    "audio/audio_service.cc"
    "audio/demuxer/ogg_demuxer.cc"
    "notify/notify_player.cc"
    "audio/codecs/no_audio_codec.cc"
    "audio/codecs/box_audio_codec.cc"
    "audio/codecs/es8311_audio_codec.cc"
    "audio/codecs/es8374_audio_codec.cc"
    "audio/codecs/es8388_audio_codec.cc"
    "audio/codecs/es8389_audio_codec.cc"
    "audio/codecs/dummy_audio_codec.cc"
    "audio/engines/afe_audio_engine.cc"
    "audio/wake_words/custom_wake_word.cc"
    "audio/wake_words/wake_word_audio_cache.cc"
    "led/single_led.cc"
    "led/circular_strip.cc"
    "led/gpio_led.cc"
    "display/display.cc"
    "display/text_glyph.cc"
    "display/lcd_display.cc"
    "display/oled_display.cc"
    "display/lvgl_display/lvgl_display.cc"
    "display/emote_display.cc"
    "display/lvgl_display/emoji_collection.cc"
    "display/lvgl_display/lvgl_theme.cc"
    "display/lvgl_display/lvgl_font.cc"
    "display/lvgl_display/dynamic_glyph_cache.cc"
    "display/lvgl_display/lvgl_image.cc"
    "display/lvgl_display/gif/lvgl_gif.cc"
    "display/lvgl_display/gif/gifdec.c"
    "display/lvgl_display/jpg/image_to_jpeg.cpp"
    "display/lvgl_display/jpg/jpeg_to_image.c"
    "protocols/protocol.cc"
    "protocols/text_glyph_payload.cc"
    "protocols/mqtt_protocol.cc"
    "protocols/websocket_protocol.cc"
    "mcp_server.cc"
    "system_info.cc"
    "application.cc"
    "ota.cc"
    "settings.cc"
    "device_state_machine.cc"
    "assets.cc"
    "boards/common/board.cc"
    "boards/common/wifi_board.cc"
    "boards/common/ml307_board.cc"
    "boards/common/nt26_board.cc"
    "boards/common/dual_network_board.cc"
    "boards/common/adc_battery_monitor.cc"
    "boards/common/axp2101.cc"
    "boards/common/backlight.cc"
    "boards/common/button.cc"
    "boards/common/i2c_device.cc"
    "boards/common/knob.cc"
    "boards/common/power_save_timer.cc"
    "boards/common/press_to_talk_mcp_tool.cc"
    "boards/common/sleep_timer.cc"
    "boards/common/sy6970.cc"
    "boards/common/system_reset.cc"
)
if(CONFIG_USE_ESP_BLUFI_WIFI_PROVISIONING)
    list(APPEND XIAOZHI_SOURCES "boards/common/blufi.cpp")
endif()
list(TRANSFORM XIAOZHI_SOURCES PREPEND "${XIAOZHI_MAIN}/")

set(XIAOZHI_INCLUDE_DIRS "." "display" "display/lvgl_display" "display/lvgl_display/jpg"
                         "audio" "audio/demuxer" "protocols" "boards/common")
list(TRANSFORM XIAOZHI_INCLUDE_DIRS PREPEND "${XIAOZHI_MAIN}/")

# --- Language: derive the locale directory from CONFIG_LANGUAGE_* -------------
# Each locale dir (e.g. pt-BR) has a matching CONFIG_LANGUAGE_PT_BR option.
file(GLOB LOCALE_DIRS LIST_DIRECTORIES true "${XIAOZHI_MAIN}/assets/locales/*")
set(LANG_DIR "")
foreach(dir ${LOCALE_DIRS})
    get_filename_component(locale "${dir}" NAME)
    string(TOUPPER "${locale}" lang_option)
    string(REPLACE "-" "_" lang_option "${lang_option}")
    if(CONFIG_LANGUAGE_${lang_option})
        set(LANG_DIR "${locale}")
    endif()
endforeach()
if(NOT LANG_DIR AND NOT CMAKE_BUILD_EARLY_EXPANSION)
    message(FATAL_ERROR "No xiaozhi locale matches the selected CONFIG_LANGUAGE_* option")
endif()

set(LANG_JSON "${XIAOZHI_MAIN}/assets/locales/${LANG_DIR}/language.json")
set(LANG_HEADER "${XIAOZHI_MAIN}/assets/lang_config.h")

# Sounds of the selected language, falling back to en-US for missing ones.
file(GLOB LANG_SOUNDS "${XIAOZHI_MAIN}/assets/locales/${LANG_DIR}/*.ogg")
if(NOT LANG_DIR STREQUAL "en-US")
    set(EXISTING_NAMES "")
    foreach(sound ${LANG_SOUNDS})
        get_filename_component(name "${sound}" NAME)
        list(APPEND EXISTING_NAMES "${name}")
    endforeach()
    file(GLOB EN_US_SOUNDS "${XIAOZHI_MAIN}/assets/locales/en-US/*.ogg")
    foreach(sound ${EN_US_SOUNDS})
        get_filename_component(name "${sound}" NAME)
        if(NOT name IN_LIST EXISTING_NAMES)
            list(APPEND LANG_SOUNDS "${sound}")
        endif()
    endforeach()
endif()
file(GLOB COMMON_SOUNDS "${XIAOZHI_MAIN}/assets/common/*.ogg")

idf_component_register(SRCS ${NB3_SOURCES} ${XIAOZHI_SOURCES}
                    EMBED_FILES ${LANG_SOUNDS} ${COMMON_SOUNDS} "wake/nexo.tflite"
                    INCLUDE_DIRS ${NB3_INCLUDE_DIRS} ${XIAOZHI_INCLUDE_DIRS}
                    WHOLE_ARCHIVE
                    PRIV_REQUIRES
                        esp_pm
                        esp_psram
                        esp_netif
                        esp_eth
                        esp_driver_gpio
                        esp_driver_uart
                        esp_driver_spi
                        esp_driver_i2c
                        esp_driver_i2s
                        esp_driver_touch_sens
                        esp_driver_ledc
                        esp_driver_jpeg
                        esp_driver_ppa
                        esp_driver_tsens
                        esp_app_format
                        app_update
                        spi_flash
                        mbedtls
                        console
                        efuse
                        bt
                        fatfs
                        nvs_flash
                        espcoredump
                        xiaozhi-fonts
                        nb_tflite
                    )

if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    # xiaozhi pins its C++ dialect; keep the same for the sources we compile.
    target_compile_options(${COMPONENT_LIB} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++23>)

    string(REGEX MATCH "^font_noto_sans_basic_([0-9]+)_([0-9]+)$" _font_match "${BUILTIN_TEXT_FONT}")
    target_compile_definitions(${COMPONENT_LIB}
        PRIVATE BOARD_TYPE=\"${BOARD_TYPE}\" BOARD_NAME=\"${BOARD_NAME}\"
        PRIVATE BOARD_MANUFACTURER=\"${BOARD_MANUFACTURER}\"
        PRIVATE BUILTIN_TEXT_FONT=${BUILTIN_TEXT_FONT} BUILTIN_ICON_FONT=${BUILTIN_ICON_FONT}
        PRIVATE TEXT_FONT_SIZE=${CMAKE_MATCH_1} TEXT_FONT_BPP=${CMAKE_MATCH_2}
    )

    # Bench build only (idf.py -DNB3_POWER_CUT_TEST=1 build): the card is
    # written nonstop so power can be cut mid-write (memory/power_cut_test).
    # The value stays in the CMake cache: pass =0 to go back.
    if(NB3_POWER_CUT_TEST)
        target_compile_definitions(${COMPONENT_LIB} PRIVATE NB3_POWER_CUT_TEST=1)
    endif()
    # Bench build only (idf.py -DNB3_MOTION_BENCH=1 build): the servos run a
    # small repeating look-around under motion_safety (motion/motion_bench).
    if(NB3_MOTION_BENCH)
        target_compile_definitions(${COMPONENT_LIB} PRIVATE NB3_MOTION_BENCH=1)
    endif()

    # NB3's own code builds warning-free; xiaozhi sources keep their flags.
    set_source_files_properties(${NB3_SOURCES} PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Werror")
    # NB3's C is the cores: plain C17, as in the host tests. Source options
    # come after the IDF's own -std, so this one wins.
    set(NB3_C_SOURCES ${NB3_SOURCES})
    list(FILTER NB3_C_SOURCES INCLUDE REGEX "\\.c$")
    set_source_files_properties(${NB3_C_SOURCES} PROPERTIES
        COMPILE_OPTIONS "-Wall;-Wextra;-Werror;-std=c17")

    # LVGL's builtin pool lives in PSRAM, as in xiaozhi.
    idf_component_get_property(lvgl_lib lvgl__lvgl COMPONENT_LIB)
    if(lvgl_lib)
        target_include_directories(${lvgl_lib} PRIVATE "${XIAOZHI_MAIN}/display")
        target_compile_definitions(${lvgl_lib} PRIVATE "LV_MEM_POOL_INCLUDE=\"lvgl_psram_pool.h\"")
    endif()
endif()

# --- Generated language header ------------------------------------------------
add_custom_command(
    OUTPUT ${LANG_HEADER}
    COMMAND python ${XIAOZHI_SCRIPTS}/gen_lang.py --language "${LANG_DIR}" --output "${LANG_HEADER}"
    DEPENDS ${LANG_JSON} ${XIAOZHI_SCRIPTS}/gen_lang.py
    COMMENT "Generating ${LANG_DIR} language config"
)
add_custom_target(lang_header ALL DEPENDS ${LANG_HEADER})

# --- Default assets partition (fonts, emoji, wake word model) -----------------
function(find_component_dir pattern out_var)
    idf_build_get_property(build_components BUILD_COMPONENTS)
    foreach(component ${build_components})
        if(component MATCHES "${pattern}")
            idf_component_get_property(dir ${component} COMPONENT_DIR)
            set(${out_var} "${dir}" PARENT_SCOPE)
            return()
        endif()
    endforeach()
endfunction()

find_component_dir("espressif__esp-sr" ESP_SR_DIR)
find_component_dir("xiaozhi-fonts" FONTS_DIR)

partition_table_get_partition_info(assets_size "--partition-name assets" "size")
if(assets_size AND CONFIG_FLASH_DEFAULT_ASSETS)
    set(GENERATED_ASSETS_BIN "${CMAKE_BINARY_DIR}/generated_assets.bin")
    file(GLOB EMOJI_FILES "${FONTS_DIR}/png/${DEFAULT_EMOJI_COLLECTION}/*")
    add_custom_command(
        OUTPUT ${GENERATED_ASSETS_BIN}
        COMMAND python ${XIAOZHI_SCRIPTS}/build_default_assets.py
                --sdkconfig "${SDKCONFIG}"
                --output "${GENERATED_ASSETS_BIN}"
                --builtin_text_font "${BUILTIN_TEXT_FONT}"
                --emoji_collection "${DEFAULT_EMOJI_COLLECTION}"
                --esp_sr_model_path "${ESP_SR_DIR}/model"
                --noto_fonts_path "${FONTS_DIR}"
                --max_size "${assets_size}"
        DEPENDS "${SDKCONFIG}" ${XIAOZHI_SCRIPTS}/build_default_assets.py ${EMOJI_FILES}
        COMMENT "Building default assets.bin"
        VERBATIM
    )
    add_custom_target(generated_default_assets ALL DEPENDS ${GENERATED_ASSETS_BIN})
    esptool_py_flash_to_partition(flash "assets" "${GENERATED_ASSETS_BIN}")
endif()

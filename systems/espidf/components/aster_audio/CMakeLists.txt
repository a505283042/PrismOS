include(${CMAKE_CURRENT_LIST_DIR}/../../../../common/sources.cmake)
set(AUDIO "${CMAKE_CURRENT_LIST_DIR}/../../../../platform/esp32s3")
if(CONFIG_ASTER_BOARD_FAKEPOD_S31)
    set(AUDIO_PORT "${CMAKE_CURRENT_LIST_DIR}/../../../../boards/fakepod_s31/idf/port/audio.c")
    set(MEDIA_SOURCES ${ASTER_COMMON_MEDIA_SOURCES} "${ASTER_COMMON_ROOT}/audio/aster_audio_spectrum.c"
        "bt_audio.c" "bt_avrcp.c" "bt_cover.c" "bt_source.c" "player.c")
elseif(CONFIG_ASTER_BOARD_OSPTEK_P4)
    set(AUDIO_PORT "port.c")
else()
    # Requirements expansion runs before newly introduced Kconfig symbols exist.
    set(AUDIO_PORT "port.c")
    if(NOT CMAKE_BUILD_EARLY_EXPANSION)
        message(FATAL_ERROR "Board has no registered Aster audio adapter")
    endif()
endif()
idf_component_register(SRCS ${MEDIA_SOURCES} ${ASTER_COMMON_AUDIO_SOURCES} "${AUDIO_PORT}" "${AUDIO}/audio.c" "${AUDIO}/audio_effect_engine.c"
    "${AUDIO}/audio_file_reader.c" "${AUDIO}/audio_lyrics.c" "${AUDIO}/minimp3_impl.c"
    INCLUDE_DIRS "../../../../contract/include" "../../../../common/audio" "." "${AUDIO}" "../../../../modules/minimp3"
    PRIV_INCLUDE_DIRS "../../../../third_party/dr_libs"
    PRIV_REQUIRES bt console nvs_flash
    REQUIRES aster_render esp_driver_i2c esp_driver_i2s esp_driver_gpio freertos)
target_compile_definitions(${COMPONENT_LIB} PUBLIC CONFIG_ASTER_AUDIO=1
    CONFIG_ASTER_AUDIO_RATE=16000 CONFIG_ASTER_AUDIO_CHANNELS=2 CONFIG_ASTER_AUDIO_BITS=16)

if(CONFIG_ASTER_BOARD_FAKEPOD_S31)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE CONFIG_ASTER_AUDIO_CS43131=1)
    target_compile_definitions(${COMPONENT_LIB} PUBLIC ASTER_AUDIO_SPECTRUM_ENABLED=1)
elseif(CONFIG_ASTER_BOARD_OSPTEK_P4)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE CONFIG_ASTER_AUDIO_ADC_ES8311=1)
endif()

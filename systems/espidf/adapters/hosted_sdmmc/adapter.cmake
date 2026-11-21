# Hosted SDIO and local storage share one IDF SDMMC controller.
set(ASTER_BOARD_WIFI_SOURCES "${CMAKE_CURRENT_LIST_DIR}/sdhost.c")
set(ASTER_BOARD_WIFI_INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}")
set(ASTER_BOARD_WIFI_REQUIRES esp_driver_sdmmc esp_wifi_remote esp_hosted)

function(aster_board_configure_wifi target)
    target_compile_definitions(${target} PUBLIC ASTER_STORAGE_SLOT=${ASTER_BOARD_STORAGE_SLOT}
                               PRIVATE ASTER_HOSTED_SLOT=${CONFIG_ESP_HOSTED_SDIO_SLOT})
    idf_component_get_property(HOSTED_LIB espressif__esp_hosted COMPONENT_LIB)
    idf_component_get_property(HOSTED_DIR espressif__esp_hosted COMPONENT_DIR)
    set_property(SOURCE "${HOSTED_DIR}/host/port/esp/freertos/src/port_esp_hosted_host_sdio.c"
        TARGET_DIRECTORY ${HOSTED_LIB} APPEND PROPERTY COMPILE_DEFINITIONS
        "sdmmc_host_init=aster_hosted_sdmmc_init"
        "sdmmc_host_deinit=aster_hosted_sdmmc_deinit")
endfunction()

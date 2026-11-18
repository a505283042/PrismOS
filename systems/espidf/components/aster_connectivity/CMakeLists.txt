set(CONNECTIVITY_SOURCES "wifi.c" "wifi_service.c" "clock.c")
set(CONNECTIVITY_DEPS esp_wifi esp_event esp_netif nvs_flash freertos console esp_driver_uart esp_driver_usb_serial_jtag)
if(IDF_TARGET STREQUAL "esp32p4")
    list(APPEND CONNECTIVITY_SOURCES "sdhost.c")
    list(APPEND CONNECTIVITY_DEPS esp_driver_sdmmc esp_wifi_remote esp_hosted)
endif()
idf_component_register(SRCS ${CONNECTIVITY_SOURCES} INCLUDE_DIRS "." "../../../../contract/include" PRIV_INCLUDE_DIRS "../../../../common/port" REQUIRES ${CONNECTIVITY_DEPS})

# Only the OSPTEK P4C5 board's external C5 shares the SDMMC host with the storage slot.
if(CONFIG_ASTER_BOARD_OSPTEK_P4 AND NOT CMAKE_BUILD_EARLY_EXPANSION)
    idf_component_get_property(HOSTED_LIB espressif__esp_hosted COMPONENT_LIB)
    idf_component_get_property(HOSTED_DIR espressif__esp_hosted COMPONENT_DIR)
    set_property(SOURCE "${HOSTED_DIR}/host/port/esp/freertos/src/port_esp_hosted_host_sdio.c"
        TARGET_DIRECTORY ${HOSTED_LIB} APPEND PROPERTY COMPILE_DEFINITIONS
        "sdmmc_host_init=aster_hosted_sdmmc_init"
        "sdmmc_host_deinit=aster_hosted_sdmmc_deinit")
endif()

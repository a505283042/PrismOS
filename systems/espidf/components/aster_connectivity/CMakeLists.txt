include("${CMAKE_CURRENT_LIST_DIR}/../../board.cmake")
if(ASTER_BOARD_WIFI_ADAPTER)
    include("${ASTER_BOARD_WIFI_ADAPTER}")
endif()

idf_component_register(
    SRCS wifi.c wifi_service.c clock.c ${ASTER_BOARD_WIFI_SOURCES}
    INCLUDE_DIRS "." "../../../../contract/include" ${ASTER_BOARD_WIFI_INCLUDE_DIRS}
    PRIV_INCLUDE_DIRS "../../../../common/port"
    REQUIRES esp_wifi esp_event esp_netif nvs_flash freertos console
             esp_driver_uart esp_driver_usb_serial_jtag ${ASTER_BOARD_WIFI_REQUIRES})

if(COMMAND aster_board_configure_wifi)
    aster_board_configure_wifi(${COMPONENT_LIB})
endif()

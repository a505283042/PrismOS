if(ASTER_S31_DIAGNOSTIC)
    idf_component_register(
        SRCS
        "diagnostic.c"
        "board_lcd.c"
        "aot_probe.c"
        INCLUDE_DIRS
        "."
        "../../../../build/fakepod-s31"
        REQUIRES
        wamr
        esp_mm
        esp_lcd
        esp_timer
        esp_lcd_co5300
        esp_driver_spi
        esp_driver_i2c
        esp_driver_gpio
        esp_psram
        spi_flash
        esp_hw_support
        fatfs
        sdmmc
        esp_driver_sdmmc)
else()
    idf_component_register(
        SRCS
        "main.c"
        "board_lcd.c"
        "board_input.c"
        "storage.c"
        "../port/audio_diagnostic.c"
        "../port/usb_console.c"
        INCLUDE_DIRS
        "."
        REQUIRES
        console
        aster_apps
        aster_transfer
        aster_connectivity
        aster_runtime
        aster_audio
        esp_lcd
        esp_timer
        esp_lcd_co5300
        esp_driver_spi
        esp_driver_i2c
        esp_driver_gpio
        esp_psram
        spi_flash
        fatfs
        sdmmc
        esp_driver_sdmmc)
    target_link_options(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=usb_serial_jtag_driver_install")
    if(CONFIG_ASTER_FAKEPOD_USB_STORAGE)
        # TinyUSB defines a weak START STOP callback in its caller's
        # compilation unit, so linker wrapping cannot intercept it.
        idf_component_get_property(ESP_TINYUSB_LIB espressif__esp_tinyusb COMPONENT_LIB)
        target_compile_definitions(
            ${ESP_TINYUSB_LIB} PRIVATE tud_msc_start_stop_cb=aster_unused_esp_tinyusb_start_stop_cb)
        target_sources(${COMPONENT_LIB} PRIVATE "usb_storage.c")
        target_include_directories(${COMPONENT_LIB} PRIVATE "../../../../common/ui")
        target_link_options(
            ${COMPONENT_LIB}
            INTERFACE
            "-Wl,--wrap=tud_msc_read10_cb,--wrap=tud_msc_write10_cb,--wrap=tud_msc_test_unit_ready_cb,--wrap=tud_msc_scsi_cb"
        )
    endif()
endif()

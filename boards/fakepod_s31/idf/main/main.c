/* SPDX-License-Identifier: MIT */
#include "board_pins.h"
#include "board_lcd.h"
#include "board_input.h"
#include "aster_idf_runtime.h"
#include "aster_idf_apps.h"
#include "aster_idf_wifi.h"
#include "aster_device.h"
#include "aster_power.h"
#include "aster_os.h"
#include "aster_services.h"
#include "aster_network.h"
#include "transfer.h"
#include "aster_idf_audio.h"
#include "aster_audio_output.h"
#include "esp_log.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_psram.h"
#include "esp_clk_tree.h"
static const char *TAG = "fakepod";
void fakepod_storage_start(void);
void fakepod_audio_diagnostic_register(void);

void app_main(void)
{
    ESP_LOGI(TAG, "Aster FAKE POD NANO PRO / ESP32-S31");
    ESP_ERROR_CHECK(fakepod_lcd_init());
    esp_err_t touch = fakepod_input_init();
    if (touch)
        ESP_LOGW(TAG, "Touch: %s", esp_err_to_name(touch));
    static aster_system_info_t identity = {.platform = "esp-idf",
                                           .board = CONFIG_ASTER_BOARD_ID,
                                           .soc = "esp32s31",
                                           .cpu_arch = ASTER_ARCH_RISCV,
                                           .cpu_freq_hz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ * 1000000,
                                           .cpu_count = 2};
    esp_flash_get_size(NULL, &identity.flash_size);
    identity.ram_external = esp_psram_get_size();
    const aster_idf_io_t io = {.width = FP_LCD_WIDTH,
                               .height = FP_LCD_HEIGHT,
                               .present = fakepod_lcd_present,
                               .imu_read = fakepod_imu_read,
                               .touch = touch == ESP_OK ? fakepod_touch_read : NULL,
                               .max_touch_points = 1,
                               .system_info = &identity,
                               .backlight = fakepod_lcd_brightness};
    aster_idf_set_io(&io);
    aster_idf_set_capture(fakepod_lcd_capture);
    fakepod_power_register();
    aster_idf_power_register();
    aster_power_seal();
    aster_device_seal();
    aster_os_seal();
    aster_services_seal();
    aster_network_seal();
    fakepod_storage_start();
    int audio = aster_idf_audio_start();
    if (audio)
        ESP_LOGW(TAG, "Audio: %d", audio);
#ifdef ASTER_IDF_MEDIA_PLAYER
    if (!audio) {
        int media = aster_idf_player_register();
        if (media)
            ESP_LOGW(TAG, "Media: %d", media);
    }
#endif
    aster_audio_output_seal();
    int apps = aster_idf_apps_start();
    if (apps)
        ESP_LOGE(TAG, "APP worker: %d", apps);
    aster_idf_set_app_command(aster_idf_apps_command);
    aster_idf_network_start();
    fakepod_audio_diagnostic_register();
#ifdef ASTER_IDF_MEDIA_PLAYER
    aster_idf_bt_audio_register();
#endif
    int transfer = aster_idf_transfer_register();
    if (transfer)
        ESP_LOGE(TAG, "Package transfer: %d", transfer);
}

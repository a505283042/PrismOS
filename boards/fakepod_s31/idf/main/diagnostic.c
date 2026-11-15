/* SPDX-License-Identifier: MIT */
#include "board_pins.h"
#include "board_lcd.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_chip_info.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "esp_flash.h"
#include "esp_psram.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
static const char *TAG="fakepod_s31";
static void probe(int port,int sda,int scl,const uint8_t *addresses,size_t count){
 i2c_master_bus_handle_t bus;
 i2c_master_bus_config_t c={.i2c_port=port,.sda_io_num=sda,.scl_io_num=scl,.clk_source=I2C_CLK_SRC_DEFAULT,.glitch_ignore_cnt=7,.flags.enable_internal_pullup=true};
 esp_err_t err=i2c_new_master_bus(&c,&bus);
 if(err!=ESP_OK){ESP_LOGE(TAG,"I2C%d create: %s",port,esp_err_to_name(err));return;}
 for(size_t i=0;i<count;i++){
  err=i2c_master_probe(bus,addresses[i],100);
  ESP_LOGI(TAG,"I2C%d address=0x%02x result=%s",port,addresses[i],esp_err_to_name(err));
 }
}
void app_main(void){
 esp_chip_info_t chip;esp_chip_info(&chip);uint32_t flash=0;esp_flash_get_size(NULL,&flash);
 ESP_LOGI(TAG,"Diagnostic entry, not the completed Aster system");
 ESP_LOGI(TAG,"cores=%d revision=%d flash=%lu PSRAM=%u free_external=%u",chip.cores,chip.revision,(unsigned long)flash,(unsigned)esp_psram_get_size(),(unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
 /* Read-only probes: do not alter undocumented PMU rail voltages. */
 const uint8_t shared[]={FP_TOUCH_ADDRESS,FP_PMU_ADDRESS,FP_IMU_ADDRESS,FP_RTC_ADDRESS};
 const uint8_t dac[]={FP_DAC_ADDRESS};
 probe(0,FP_I2C0_SDA,FP_I2C0_SCL,shared,sizeof(shared));
 gpio_config_t dac_reset={.pin_bit_mask=1ULL<<FP_DAC_RESET,.mode=GPIO_MODE_OUTPUT};
 gpio_config(&dac_reset);gpio_set_level(FP_DAC_RESET,0);vTaskDelay(pdMS_TO_TICKS(2));gpio_set_level(FP_DAC_RESET,1);vTaskDelay(pdMS_TO_TICKS(20));
 probe(1,FP_DAC_SDA,FP_DAC_SCL,dac,sizeof(dac));
 sdmmc_host_t host=SDMMC_HOST_DEFAULT();host.max_freq_khz=20000;
 sdmmc_slot_config_t slot=SDMMC_SLOT_CONFIG_DEFAULT();slot.width=4;
 slot.clk=FP_SD_CLOCK;slot.cmd=FP_SD_CMD;slot.d0=FP_SD_D0;slot.d1=FP_SD_D1;slot.d2=FP_SD_D2;slot.d3=FP_SD_D3;
 const esp_vfs_fat_sdmmc_mount_config_t mount={.format_if_mount_failed=false,.max_files=4,.allocation_unit_size=16384};
 sdmmc_card_t *card=NULL;esp_err_t err=esp_vfs_fat_sdmmc_mount("/sdcard",&host,&slot,&mount,&card);
 ESP_LOGI(TAG,"SD mount result=%s (format disabled)",esp_err_to_name(err));
 if(err==ESP_OK)sdmmc_card_print_info(stdout,card);
 esp_err_t display=fakepod_lcd_init();ESP_LOGI(TAG,"LCD init: %s",esp_err_to_name(display));
 if(display==ESP_OK){
  uint16_t *pixels=heap_caps_malloc(FP_LCD_WIDTH*FP_LCD_HEIGHT*2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  if(pixels){const uint16_t colors[]={0xf800,0x07e0,0x001f,0xffff,0x0000,0xffe0};for(int y=0;y<FP_LCD_HEIGHT;y++)for(int x=0;x<FP_LCD_WIDTH;x++)pixels[y*FP_LCD_WIDTH+x]=colors[x*6/FP_LCD_WIDTH];fakepod_lcd_present((uint8_t*)pixels,0,0,FP_LCD_WIDTH,FP_LCD_HEIGHT,FP_LCD_WIDTH);free(pixels);}
 }
 extern void fakepod_aot_probe(void);fakepod_aot_probe();
 for(;;)vTaskDelay(pdMS_TO_TICKS(1000));
}

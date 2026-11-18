/* SPDX-License-Identifier: MIT */
#include "board_lcd.h"
#include <string.h>
#include "board_pins.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_co5300.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
static esp_lcd_panel_handle_t panel;
static esp_lcd_panel_io_handle_t panel_io;
static uint8_t *snapshot;
static SemaphoreHandle_t completed;
static uint16_t *bounce;
static bool panel_blank;
static bool transfer_done(esp_lcd_panel_io_handle_t io,esp_lcd_panel_io_event_data_t *event,void *context){
 (void)io;(void)event;BaseType_t wake=pdFALSE;xSemaphoreGiveFromISR((SemaphoreHandle_t)context,&wake);return wake==pdTRUE;
}
esp_err_t fakepod_lcd_init(void){
 gpio_config_t power={.pin_bit_mask=1ULL<<FP_LCD_POWER,.mode=GPIO_MODE_OUTPUT};
 ESP_ERROR_CHECK(gpio_config(&power));gpio_set_level(FP_LCD_POWER,0);vTaskDelay(pdMS_TO_TICKS(10));gpio_set_level(FP_LCD_POWER,1);vTaskDelay(pdMS_TO_TICKS(120));
 completed=xSemaphoreCreateBinary();bounce=heap_caps_malloc(FP_LCD_WIDTH*16*2,MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL);
 if(!completed||!bounce)return ESP_ERR_NO_MEM;
 spi_bus_config_t bus={.sclk_io_num=FP_LCD_CLOCK,.data0_io_num=FP_LCD_D0,.data1_io_num=FP_LCD_D1,.data2_io_num=FP_LCD_D2,.data3_io_num=FP_LCD_D3,.data4_io_num=-1,.data5_io_num=-1,.data6_io_num=-1,.data7_io_num=-1,.max_transfer_sz=FP_LCD_WIDTH*16*2,.flags=SPICOMMON_BUSFLAG_QUAD};
 esp_err_t e=spi_bus_initialize(SPI2_HOST,&bus,SPI_DMA_CH_AUTO);if(e)return e;
 esp_lcd_panel_io_handle_t io;
 esp_lcd_panel_io_spi_config_t cfg=CO5300_PANEL_IO_QSPI_CONFIG(FP_LCD_CS,transfer_done,completed);cfg.pclk_hz=40000000;cfg.trans_queue_depth=1;
 e=esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST,&cfg,&io);if(e)return e;panel_io=io;
 snapshot=heap_caps_calloc(FP_LCD_WIDTH*FP_LCD_HEIGHT,2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(!snapshot)return ESP_ERR_NO_MEM;
 co5300_vendor_config_t vendor={.flags.use_qspi_interface=1};
 const esp_lcd_panel_dev_config_t config={.reset_gpio_num=FP_LCD_RESET,.rgb_ele_order=LCD_RGB_ELEMENT_ORDER_RGB,.bits_per_pixel=16,.vendor_config=&vendor};
 e=esp_lcd_new_panel_co5300(io,&config,&panel);if(e)return e;
 e=esp_lcd_panel_reset(panel);if(e)return e;e=esp_lcd_panel_init(panel);if(e)return e;
 e=esp_lcd_panel_set_gap(panel,FP_LCD_X_OFFSET,0);if(e)return e;
 return esp_lcd_panel_disp_on_off(panel,true);
}
static int transfer_snapshot(uint32_t x,uint32_t y,uint32_t w,uint32_t h)
{
 for(uint32_t row=0;row<h;row+=16){
  uint32_t rows=h-row;if(rows>16)rows=16;
  for(uint32_t yy=0;yy<rows;yy++){
   const uint8_t *src=snapshot+((size_t)(y+row+yy)*FP_LCD_WIDTH+x)*2;
   uint8_t *dst=(uint8_t *)bounce+(size_t)yy*w*2;
   for(uint32_t col=0;col<w*2;col+=2){
    dst[col]=src[col+1];
    dst[col+1]=src[col];
   }
  }
  xSemaphoreTake(completed,0);
  if(esp_lcd_panel_draw_bitmap(panel,x,y+row,x+w,y+row+rows,bounce)!=ESP_OK)return -1;
  xSemaphoreTake(completed,portMAX_DELAY);
 }
 return 0;
}

int fakepod_lcd_present(const uint8_t *pixels,uint32_t x,uint32_t y,uint32_t w,uint32_t h,uint32_t pitch){
 if(!panel||!pixels||!w||!h||x>=FP_LCD_WIDTH||y>=FP_LCD_HEIGHT||w>FP_LCD_WIDTH-x||h>FP_LCD_HEIGHT-y||pitch<w)return -1;
 /* Capture shares the retained image in native RGB565 order. Convert
  * only in the SPI buffer, including when replaying the image on wake. */
 for(uint32_t yy=0;yy<h;yy++){
  const uint8_t *src=pixels+(size_t)yy*pitch*2;
  uint8_t *dst=snapshot+((size_t)(y+yy)*FP_LCD_WIDTH+x)*2;
  memcpy(dst,src,w*2);
 }
 if(panel_blank)return 0;
 /* The CO5300 only accepts complete 2x2 pixel cells. */
 uint32_t right=(x+w+1)&~1u,bottom=(y+h+1)&~1u;
 x&=~1u;y&=~1u;
 return transfer_snapshot(x,y,right-x,bottom-y);
}

const uint8_t *fakepod_lcd_capture(void){return snapshot;}
static esp_err_t set_brightness(uint8_t value)
{
 return esp_lcd_panel_io_tx_param(panel_io,0x02005100,&value,1);
}

int fakepod_lcd_brightness(bool on,uint8_t level)
{
 if(!panel_io||!panel)return -1;
 if(!on){
  if(panel_blank)return 0;
  if(set_brightness(0)!=ESP_OK)return -1;
  panel_blank=true;
  esp_err_t result=esp_lcd_panel_disp_on_off(panel,false);
  if(result==ESP_OK)result=esp_lcd_panel_disp_sleep(panel,true);
  ESP_LOGI("fakepod_lcd","panel blank, sleep: %s",esp_err_to_name(result));
  return 0;
 }
 if(panel_blank){
  if(esp_lcd_panel_disp_sleep(panel,false)!=ESP_OK||
     set_brightness(0)!=ESP_OK||
     transfer_snapshot(0,0,FP_LCD_WIDTH,FP_LCD_HEIGHT)!=0||
     esp_lcd_panel_disp_on_off(panel,true)!=ESP_OK||
     set_brightness(level)!=ESP_OK)return -1;
  panel_blank=false;
  ESP_LOGI("fakepod_lcd","panel awake, frame restored");
  return 0;
 }
 return set_brightness(level)==ESP_OK?0:-1;
}

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
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
static esp_lcd_panel_handle_t panel;
static esp_lcd_panel_io_handle_t panel_io;
static uint8_t *snapshot;
static SemaphoreHandle_t completed;
static uint16_t *bounce;
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
int fakepod_lcd_present(const uint8_t *pixels,uint32_t x,uint32_t y,uint32_t w,uint32_t h,uint32_t pitch){
 if(!panel||!pixels||!w||!h||x>=FP_LCD_WIDTH||y>=FP_LCD_HEIGHT||w>FP_LCD_WIDTH-x||h>FP_LCD_HEIGHT-y||pitch<w)return -1;
 /* CO5300 partial windows cover complete 2x2 pixel cells. Merge first,
  * then expand the transfer from the retained image, including unchanged
  * neighbours. This applies equally to LVGL and APP single-pixel updates. */
 for(uint32_t yy=0;yy<h;yy++)for(uint32_t xx=0;xx<w;xx++){
  const uint8_t *p=pixels+((size_t)yy*pitch+xx)*2;
  size_t offset=((size_t)(y+yy)*FP_LCD_WIDTH+x+xx)*2;
  snapshot[offset]=p[1];snapshot[offset+1]=p[0];
 }
 uint32_t right=(x+w+1)&~1u,bottom=(y+h+1)&~1u;
 x&=~1u;y&=~1u;w=right-x;h=bottom-y;
 for(uint32_t row=0;row<h;row+=16){
  uint32_t rows=h-row;if(rows>16)rows=16;
  for(uint32_t yy=0;yy<rows;yy++)
   memcpy((uint8_t *)bounce+(size_t)yy*w*2,snapshot+((size_t)(y+row+yy)*FP_LCD_WIDTH+x)*2,w*2);
  xSemaphoreTake(completed,0);
  if(esp_lcd_panel_draw_bitmap(panel,x,y+row,x+w,y+row+rows,bounce)!=ESP_OK)return -1;
  xSemaphoreTake(completed,portMAX_DELAY);
 }
 return 0;
}


const uint8_t *fakepod_lcd_capture(void){return snapshot;}
int fakepod_lcd_brightness(bool on,uint8_t level){
 if(!panel_io)return -1;
 uint8_t value=on?level:0;
 return esp_lcd_panel_io_tx_param(panel_io,0x02005100,&value,1)==ESP_OK?0:-1;
}

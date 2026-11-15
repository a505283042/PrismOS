/* SPDX-License-Identifier: MIT */
#include "board_input.h"
#include "board_pins.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
static i2c_master_dev_handle_t touch,imu;
static bool imu_ready;
static esp_err_t read_register(i2c_master_dev_handle_t d,uint8_t reg,void *data,size_t size){
 return i2c_master_transmit_receive(d,&reg,1,data,size,100);
}
static esp_err_t write_register(i2c_master_dev_handle_t d,uint8_t reg,uint8_t value){
 uint8_t bytes[]={reg,value};return i2c_master_transmit(d,bytes,sizeof(bytes),100);
}
static void imu_init(i2c_master_bus_handle_t bus){
 i2c_device_config_t cfg={.dev_addr_length=I2C_ADDR_BIT_LEN_7,.device_address=FP_IMU_ADDRESS,.scl_speed_hz=400000};
 if(i2c_master_bus_add_device(bus,&cfg,&imu)!=ESP_OK)return;
 uint8_t id=0;if(read_register(imu,0,&id,1)!=ESP_OK||id!=0x05)return;
 /* QST QMI8658C Rev A: little endian, auto increment, 4g/512dps at 112Hz.
  * Synchronous sampling locks the six axes until GZ_H is read. */
 const uint8_t settings[][2]={{0x08,0x00},{0x02,0x40},{0x03,0x16},{0x04,0x56},{0x08,0x83}};
 for(unsigned i=0;i<sizeof(settings)/sizeof(settings[0]);i++)
  if(write_register(imu,settings[i][0],settings[i][1])!=ESP_OK)return;
 vTaskDelay(pdMS_TO_TICKS(60));imu_ready=true;
 float ax=0,ay=0,az=0,gx=0,gy=0,gz=0;int r=fakepod_imu_read(&ax,&ay,&az,&gx,&gy,&gz);
 ESP_LOGI("fakepod","QMI8658 sample status=%d accel=%.3f,%.3f,%.3f gyro=%.3f,%.3f,%.3f",r,ax,ay,az,gx,gy,gz);
 if(r)imu_ready=false;
}
bool fakepod_imu_available(void){return imu_ready;}
int fakepod_imu_read(float *ax,float *ay,float *az,float *gx,float *gy,float *gz){
 if(!imu_ready)return -19;
 uint8_t data[12];if(read_register(imu,0x35,data,sizeof(data))!=ESP_OK)return -5;
 float values[6];for(unsigned i=0;i<6;i++){
  int16_t raw=(int16_t)((uint16_t)data[i*2]|((uint16_t)data[i*2+1]<<8));
  values[i]=raw*(i<3?(4.0f*9.80665f/32768.0f):(512.0f*0.01745329252f/32768.0f));
 }
 *ax=values[0];*ay=values[1];*az=values[2];*gx=values[3];*gy=values[4];*gz=values[5];return 0;
}
esp_err_t fakepod_input_init(void){
 i2c_master_bus_handle_t bus;
 i2c_master_bus_config_t cfg={.i2c_port=0,.sda_io_num=FP_I2C0_SDA,.scl_io_num=FP_I2C0_SCL,
  .clk_source=I2C_CLK_SRC_DEFAULT,.glitch_ignore_cnt=7,.flags.enable_internal_pullup=true};
 esp_err_t err=i2c_new_master_bus(&cfg,&bus);if(err)return err;
 imu_init(bus);
 gpio_config_t reset={.pin_bit_mask=1ULL<<FP_TOUCH_RESET,.mode=GPIO_MODE_OUTPUT};
 err=gpio_config(&reset);if(err)return err;
 gpio_set_level(FP_TOUCH_RESET,0);vTaskDelay(pdMS_TO_TICKS(10));
 gpio_set_level(FP_TOUCH_RESET,1);vTaskDelay(pdMS_TO_TICKS(120));
 i2c_device_config_t device={.dev_addr_length=I2C_ADDR_BIT_LEN_7,.device_address=FP_TOUCH_ADDRESS,.scl_speed_hz=400000};
 err=i2c_master_bus_add_device(bus,&device,&touch);if(err)return err;
 uint8_t reg=0xa7,id=0;
 err=i2c_master_transmit_receive(touch,&reg,1,&id,1,100);
 ESP_LOGI("fakepod","CST820 ID=0x%02x status=%s",id,esp_err_to_name(err));
 return err;
}
int fakepod_touch_read(uint32_t *x,uint32_t *y){
 if(!touch)return -1;
 uint8_t reg=0x02,data[5];
 if(i2c_master_transmit_receive(touch,&reg,1,data,sizeof(data),30)!=ESP_OK)return -1;
 if(!(data[0]&0x0f))return 0;
 uint32_t px=((data[1]&0x0f)<<8)|data[2],py=((data[3]&0x0f)<<8)|data[4];
 if(px>=FP_LCD_WIDTH||py>=FP_LCD_HEIGHT)return -1;
 *x=px;*y=py;return 1;
}

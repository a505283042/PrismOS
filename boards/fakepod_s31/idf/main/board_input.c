/* SPDX-License-Identifier: MIT */
#include "board_input.h"
#include "board_pins.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "aster_power.h"
#include "aster_idf_apps.h"
#include "esp_sleep.h"
#include "aster_os.h"
#if CONFIG_ASTER_FAKEPOD_USB_STORAGE
#include "usb_storage.h"
#endif
#include <errno.h>
#include <stdio.h>
static i2c_master_dev_handle_t touch,imu,pmu;
static bool pmu_ready;
static bool pmu_key_ready, pmu_key_down;
enum {
 FP_POWER_KEY = FP_PMU_IRQ,
 AXP_KEY_RELEASE = 1u << 0,
 AXP_KEY_PRESS = 1u << 1,
 AXP_KEY_EDGES = AXP_KEY_RELEASE | AXP_KEY_PRESS
};
static bool imu_ready;
static esp_err_t read_register(i2c_master_dev_handle_t d,uint8_t reg,void *data,size_t size){
 return i2c_master_transmit_receive(d,&reg,1,data,size,100);
}
static esp_err_t write_register(i2c_master_dev_handle_t d,uint8_t reg,uint8_t value){
 uint8_t bytes[]={reg,value};return i2c_master_transmit(d,bytes,sizeof(bytes),100);
}
static bool pmu_key_init(void)
{
 const gpio_config_t irq={.pin_bit_mask=1ULL<<FP_PMU_IRQ,.mode=GPIO_MODE_INPUT,.pull_up_en=true};
 if(gpio_config(&irq)!=ESP_OK)return false;
 uint8_t timing, enabled;
 if(read_register(pmu,0x27,&timing,1)||read_register(pmu,0x41,&enabled,1))return false;
 /* ONLEVEL=1 s, OFFLEVEL=10 s. Keep the PMIC emergency cutoff, with
  * time for the firmware to show the status and safely shut down first. */
 timing=(timing&~0x0f)|0x0e;
 if(write_register(pmu,0x27,timing)||write_register(pmu,0x49,AXP_KEY_EDGES)||
    write_register(pmu,0x41,enabled|AXP_KEY_EDGES))return false;
 ESP_LOGI("fakepod","AXP2101 upper key ready, power-on 1 s, emergency cutoff 10 s");
 return true;
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
 i2c_device_config_t pmu_cfg={.dev_addr_length=I2C_ADDR_BIT_LEN_7,.device_address=FP_PMU_ADDRESS,.scl_speed_hz=400000};
 if(i2c_master_bus_add_device(bus,&pmu_cfg,&pmu)==ESP_OK){
  uint8_t id=0;if(read_register(pmu,0x03,&id,1)==ESP_OK&&(id&0xcf)==0x4a)pmu_ready=true;
  ESP_LOGI("fakepod","AXP2101 ID=0x%02x ready=%d",id,pmu_ready);
  if(pmu_ready)pmu_key_ready=pmu_key_init();
 }
 gpio_config_t key_cfg={.pin_bit_mask=1ULL<<FP_BUTTON,.mode=GPIO_MODE_INPUT,.pull_up_en=true};
 ESP_ERROR_CHECK(gpio_config(&key_cfg));
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

/* Read-only AXP2101 telemetry: STATUS1 0x00, STATUS2 0x01, SOC 0xa4.
 * Register semantics cross-checked against XPowersLib AXP2101 implementation.
 * Do not alter charger, fuel-gauge calibration, or regulator settings. */
static int power_battery(void *context,aster_power_battery_t *out)
{
 (void)context;if(!pmu_ready)return -ENODEV;
 uint8_t status[2];if(read_register(pmu,0x00,status,sizeof(status)))return -EIO;
 *out=(aster_power_battery_t){.size=sizeof(*out),.version=ASTER_POWER_API_VERSION};
 if(status[0]&0x20)out->flags|=ASTER_POWER_EXTERNAL_PRESENT;
 if(status[0]&0x08){
  out->flags|=ASTER_POWER_BATTERY_PRESENT;
  if(((status[1]>>5)&3)==1)out->flags|=ASTER_POWER_CHARGING;
  uint8_t percent;if(read_register(pmu,0xa4,&percent,1))return -EIO;
  if(percent<=100){out->percent=percent;out->flags|=ASTER_POWER_PERCENT_VALID;}
 }
 return 0;
}
static int power_caps(void *context,aster_power_caps_t *out)
{
 int r=aster_idf_power_base.caps(context,out);if(r)return r;
 out->battery=pmu_ready;out->keys=true;
 out->modes|=ASTER_POWER_SCREEN_OFF;out->screen_wake|=ASTER_WAKE_KEY;
#if CONFIG_ASTER_FAKEPOD_USB_STORAGE
 out->modes|=ASTER_POWER_USB_STORAGE;
#endif
#ifdef CONFIG_ASTER_FAKEPOD_EXPERIMENTAL_LIGHT_SLEEP
 out->modes|=ASTER_POWER_LIGHT_SLEEP;
 out->light_wake=ASTER_WAKE_KEY|ASTER_WAKE_TOUCH|ASTER_WAKE_TIMER;
#endif
 if(pmu_ready)out->modes|=ASTER_POWER_SHUTDOWN;
 return 0;
}
static int power_shutdown(void *context)
{
 (void)context;if(!pmu_ready)return -ENODEV;
 uint8_t value;if(read_register(pmu,0x10,&value,1))return -EIO;
 /* AXP2101 software-off only; regulator/charger calibration remains intact. */
 if(write_register(pmu,0x10,value|1))return -EIO;
 ESP_LOGI("fakepod","AXP2101 shutdown requested; storage remains unmounted");
 for(;;)vTaskDelay(pdMS_TO_TICKS(1000));
}
static int power_suspend(void *context,const aster_power_request_t *request)
{
 (void)context;if(request->mode!=ASTER_POWER_LIGHT_SLEEP)return -ENOTSUP;
#ifndef CONFIG_ASTER_FAKEPOD_EXPERIMENTAL_LIGHT_SLEEP
 return -ENOTSUP;
#else
 uint64_t pins=0;
 if(request->wake_sources&ASTER_WAKE_TOUCH)pins|=1ULL<<FP_TOUCH_IRQ;
 if(request->wake_sources&ASTER_WAKE_KEY)pins|=1ULL<<FP_BUTTON;
 gpio_config_t config={.pin_bit_mask=pins,.mode=GPIO_MODE_INPUT,.pull_up_en=true};
 if(pins&&gpio_config(&config))return -EIO;
 for(unsigned pin=0;pin<64;pin++)if(pins&(1ULL<<pin)){
  if(!gpio_get_level(pin))return -EBUSY;
 }
 esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
 esp_err_t r=ESP_OK;
 for(unsigned pin=0;pin<64;pin++)if(pins&(1ULL<<pin))if(gpio_wakeup_enable(pin,GPIO_INTR_LOW_LEVEL)!=ESP_OK){r=ESP_FAIL;break;}
 if(!r&&pins)r=esp_sleep_enable_gpio_wakeup();
 if(!r&&(request->wake_sources&ASTER_WAKE_TIMER))r=esp_sleep_enable_timer_wakeup(request->timer_ms*1000);
 ESP_LOGI("fakepod","light sleep entering: setup=%s sources=%lu timer_ms=%llu",esp_err_to_name(r),(unsigned long)request->wake_sources,(unsigned long long)request->timer_ms);
 fflush(stdout);
 if(!r)r=esp_light_sleep_start();
 esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
 for(unsigned pin=0;pin<64;pin++)if(pins&(1ULL<<pin))gpio_wakeup_disable(pin);
 ESP_LOGI("fakepod","light sleep returned result=%s wake=%d",esp_err_to_name(r),esp_sleep_get_wakeup_cause());
 return r?-EIO:0;
#endif
}
static int power_keys(void *context,aster_power_key_t *out,uint32_t capacity)
{
 (void)context;
 unsigned count=0;
 static bool candidate,stable;static uint64_t changed;
 uint64_t now=aster_os_uptime_ms();bool pressed=!gpio_get_level(FP_BUTTON);
 if(pressed!=candidate){candidate=pressed;changed=now;}
 if(capacity&&candidate!=stable&&now-changed>=30){
  stable=candidate;
  out[count++]=(aster_power_key_t){.code=FP_BUTTON,.pressed=stable,.timestamp_ms=now};
 }
 if(!pmu_key_ready||capacity-count<2||gpio_get_level(FP_PMU_IRQ))return count;
 static uint64_t last_poll;
 if(now-last_poll<15)return count;
 last_poll=now;
 uint8_t status;
 if(read_register(pmu,0x49,&status,1))return count;
 status&=AXP_KEY_EDGES;
 if(!status||write_register(pmu,0x49,status))return count;
 if(!pmu_key_down&&(status&AXP_KEY_PRESS)){
  pmu_key_down=true;
  out[count++]=(aster_power_key_t){.code=FP_POWER_KEY,.pressed=true,.timestamp_ms=now};
 }
 if(pmu_key_down&&(status&AXP_KEY_RELEASE)){
  pmu_key_down=false;
  out[count++]=(aster_power_key_t){.code=FP_POWER_KEY,.pressed=false,.timestamp_ms=now};
 }
 return count;
}
static aster_power_key_action_t power_key_map(void *context,uint32_t code,bool pressed,uint32_t held_ms)
{
 (void)context;
 if(code==FP_POWER_KEY){
  if(held_ms>=3000)return ASTER_KEY_ACTION_SHUTDOWN;
  if(!pressed)return ASTER_KEY_ACTION_SCREEN;
 }
 if(code==FP_BUTTON&&!pressed)
  return held_ms>=800?ASTER_KEY_ACTION_HOME:ASTER_KEY_ACTION_NONE;
 return ASTER_KEY_ACTION_NONE;
}
void fakepod_power_register(void)
{
 static aster_power_ops_t ops;
 const aster_power_ops_t overrides={.size=sizeof(overrides),.version=ASTER_POWER_API_VERSION,
  .name="fakepod-s31-axp2101",.caps=power_caps,.battery=power_battery,.keys=power_keys,.map_key=power_key_map,.shutdown=power_shutdown,.suspend=power_suspend,
#if CONFIG_ASTER_FAKEPOD_USB_STORAGE
  .usb_storage=fakepod_usb_storage,
#endif
 };
 if(!aster_power_derive(&ops,&aster_idf_power_base,&overrides)){
  int r=aster_power_bind(&ops,NULL);ESP_LOGI("fakepod","Power adapter: %d",r);
 }
}

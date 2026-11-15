/* SPDX-License-Identifier: MIT */
/* CS43131 register programming follows Cirrus DS1155F2 sections 4.13/5.7/7.
 * Public PCM stays s16le stereo/16 kHz. The port converts low rates to a
 * supported DAC clock; PDM input is independently clocked at the ABI rate. */
#include "../main/board_pins.h"
#include "driver/i2s_std.h"
#include "driver/i2s_pdm.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "audio_port.h"
#include "aster_platform.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <stdatomic.h>
#include <string.h>
const struct device aster_idf_gpio_device={.role=3};
static const struct device control={.role=1},stream={.role=2};
static i2c_master_dev_handle_t codec;
static i2s_chan_handle_t tx,rx;
static struct i2s_config tx_config,rx_config;
static bool ready,available,tx_on,rx_on;
static atomic_bool cancelled;
static unsigned ratio=1,physical_rate;
static int32_t *converted;
static size_t converted_capacity;
static int16_t previous[2];
static bool have_previous;
static int error(esp_err_t r){return r==ESP_OK?0:r==ESP_ERR_TIMEOUT?-ETIMEDOUT:r==ESP_ERR_NO_MEM?-ENOMEM:-EIO;}
static int reg_read(uint32_t reg,uint8_t *value){
 uint8_t address[]={reg>>16,reg>>8,reg,0};
 return error(i2c_master_transmit_receive(codec,address,sizeof(address),value,1,200));
}
static int reg_write(uint32_t reg,uint8_t value){
 uint8_t bytes[]={reg>>16,reg>>8,reg,0,value};
 return error(i2c_master_transmit(codec,bytes,sizeof(bytes),200));
}
const struct device *aster_idf_audio_i2c(void){
 if(codec)return &control;
 gpio_config_t pin={.pin_bit_mask=1ULL<<FP_DAC_RESET,.mode=GPIO_MODE_OUTPUT};
 if(gpio_config(&pin)!=ESP_OK)return NULL;
 gpio_set_level(FP_DAC_RESET,0);k_msleep(2);gpio_set_level(FP_DAC_RESET,1);k_msleep(20);
 i2c_master_bus_handle_t bus;
 if(i2c_master_get_bus_handle(1,&bus)!=ESP_OK){
  i2c_master_bus_config_t config={.i2c_port=1,.sda_io_num=FP_DAC_SDA,.scl_io_num=FP_DAC_SCL,
   .clk_source=I2C_CLK_SRC_DEFAULT,.glitch_ignore_cnt=7,.flags.enable_internal_pullup=true};
  if(i2c_new_master_bus(&config,&bus)!=ESP_OK)return NULL;
 }
 i2c_device_config_t config={.dev_addr_length=I2C_ADDR_BIT_LEN_7,.device_address=FP_DAC_ADDRESS,.scl_speed_hz=400000};
 if(i2c_master_bus_add_device(bus,&config,&codec)!=ESP_OK)return NULL;
 return &control;
}
const struct device *aster_idf_audio_i2s(void){return &stream;}
bool device_is_ready(const struct device *d){return d && (d->role!=1 || codec);}
int gpio_is_ready_dt(const struct gpio_dt_spec *p){return p && p->port && GPIO_IS_VALID_OUTPUT_GPIO((int)p->pin);}
int gpio_pin_configure_dt(const struct gpio_dt_spec *p,int mode){(void)p;(void)mode;return -ENOTSUP;}
int gpio_pin_set_dt(const struct gpio_dt_spec *p,int value){(void)p;(void)value;return -ENOTSUP;}
/* Legacy 8-bit-register codecs are deliberately unavailable on this port. */
int i2c_reg_write_byte(const struct device *d,uint8_t a,uint8_t r,uint8_t v){(void)d;(void)a;(void)r;(void)v;return -ENODEV;}
int i2c_reg_read_byte(const struct device *d,uint8_t a,uint8_t r,uint8_t *v){(void)d;(void)a;(void)r;(void)v;return -ENODEV;}
int aster_idf_codec_volume(uint8_t percent){
 if(!ready)return -ENODEV;
 if(percent>100)percent=100;
 /* 0.5 dB/step attenuation; no positive digital gain on headphone output. */
 uint8_t attenuation=percent?((100-percent)*120/100):0xff;
 int r=reg_write(0x90002,attenuation);if(r)return r;
 return reg_write(0x90003,percent?0xec:0xef);
}
int aster_idf_codec_init(void){
 if(ready)return 0;
 if(!aster_idf_audio_i2c())return -ENODEV;
 uint8_t id[3];for(unsigned i=0;i<3;i++){int r=reg_read(0x10000+i,&id[i]);if(r)return r;}
 if(id[0]!=0x43||id[1]!=0x13||(id[2]&0xf0)!=0x10)return -ENODEV;
 static const struct {uint32_t address;uint8_t value;} settings[]={
  {0x20052,0x06}, /* 24.576 MHz crystal, 7.5 uA bias. */
  {0x20000,0xf6}, /* XTAL on; HP/ASP/PLL remain off. */
  {0x1000b,0x02},{0x1000c,0x04}, /* 48k / 32-bit ASP samples. */
  {0x40018,0x0c},{0x40019,0x0a}, /* I2S slave, one clock delay. */
  {0x50000,0},{0x50001,0},{0x5000a,0x07},{0x5000b,0x0f},
  {0x90000,0x02},{0x90001,0},{0x90002,0x30},{0x90003,0xef},
  {0x90004,0},{0xb0000,0x1e},{0x80000,0x00},{0x1000d,0x03}
 };
 for(unsigned i=0;i<sizeof(settings)/sizeof(settings[0]);i++){
  int r=reg_write(settings[i].address,settings[i].value);if(r)return r;
 }
 bool crystal=false;
 for(unsigned i=0;i<100;i++){uint8_t status;int r=reg_read(0xf0000,&status);if(r)return r;if(status&0x10){crystal=true;break;}k_msleep(2);}
 if(!crystal){ESP_LOGE("cs43131","Crystal not ready");return -ETIMEDOUT;}
 int r=reg_write(0x10006,0x00);if(r)return r; /* Direct 24.576 MHz. */
 k_msleep(1);ready=true;physical_rate=48000;
 ESP_LOGI("cs43131","verified device 43:13:10; 24.576 MHz XTAL, I2S slave");
 return aster_idf_codec_volume(70);
}
int aster_idf_codec_enable(void){
 if(!ready)return -ENODEV;
 int r=reg_write(0x10010,0x99);if(r)return r;
 r=reg_write(0x80032,0x20);if(r)return r;
 r=reg_write(0x20000,0xb6);if(r)return r;
 r=reg_write(0x20000,0xa6);if(r)return r;k_msleep(12);
 r=reg_write(0x80032,0);if(r)return r;return reg_write(0x10010,0);
}
void aster_idf_audio_cancel_record(bool v){atomic_store(&cancelled,v);}
bool aster_idf_audio_record_cancelled(void){return atomic_load(&cancelled);}
int aster_idf_audio_start(void){uint8_t a,b;int r=aster_plat_audio_probe(&a,&b);available=!r;return r;}
bool aster_idf_audio_available(void){return available;}
static void shutdown(void){
 if(tx_on)i2s_channel_disable(tx);
 if(rx_on)i2s_channel_disable(rx);
 tx_on=rx_on=false;
 if(tx)i2s_del_channel(tx);
 if(rx)i2s_del_channel(rx);
 tx=rx=NULL;
 free(converted);converted=NULL;converted_capacity=0;have_previous=false;
 memset(&tx_config,0,sizeof(tx_config));memset(&rx_config,0,sizeof(rx_config));
}
int i2s_configure(const struct device *d,enum i2s_dir dir,const struct i2s_config *config){
 (void)d;if(!config->frame_clk_freq){shutdown();return 0;}
 if(config->word_size!=16||config->channels!=2||!config->mem_slab||!config->block_size)return -EINVAL;
 if(dir==I2S_DIR_TX){
  shutdown();unsigned rate=config->frame_clk_freq;ratio=1;
  switch(rate){case 8000:ratio=6;break;case 11025:ratio=4;break;case 12000:ratio=4;break;
   case 16000:ratio=3;break;case 22050:ratio=2;break;case 24000:ratio=2;break;default:break;}
  rate*=ratio;uint8_t code;
  switch(rate){case 32000:code=0;break;case 44100:code=1;break;case 48000:code=2;break;case 88200:code=3;break;case 96000:code=4;break;default:return -EINVAL;}
  int r=reg_write(0x20000,0xf6);if(r)return r;
  r=reg_write(0x1000b,code);if(r)return r;physical_rate=rate;
  converted_capacity=config->block_size/4*ratio*2*sizeof(int32_t);
  converted=heap_caps_malloc(converted_capacity,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(!converted)return -ENOMEM;
  i2s_chan_config_t channel=I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1,I2S_ROLE_MASTER);
  channel.dma_desc_num=8;channel.dma_frame_num=128;channel.auto_clear_after_cb=true;
  esp_err_t status=i2s_new_channel(&channel,&tx,NULL);
  i2s_std_config_t standard={.clk_cfg=I2S_STD_CLK_DEFAULT_CONFIG(rate),
   .slot_cfg=I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT,I2S_SLOT_MODE_STEREO),
   .gpio_cfg={.mclk=I2S_GPIO_UNUSED,.bclk=FP_DAC_BCLK,.ws=FP_DAC_WS,.dout=FP_DAC_DOUT,.din=I2S_GPIO_UNUSED}};
  if(status==ESP_OK)status=i2s_channel_init_std_mode(tx,&standard);
  if(status!=ESP_OK){shutdown();return error(status);}tx_config=*config;
  return aster_idf_codec_enable();
 }
 if(dir!=I2S_DIR_RX)return -EINVAL;
 if(rx_on){i2s_channel_disable(rx);rx_on=false;}if(rx)i2s_del_channel(rx);rx=NULL;
 i2s_chan_config_t channel=I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0,I2S_ROLE_MASTER);channel.dma_desc_num=6;channel.dma_frame_num=128;
 esp_err_t status=i2s_new_channel(&channel,NULL,&rx);
 i2s_pdm_rx_config_t pdm={.clk_cfg=I2S_PDM_RX_CLK_DEFAULT_CONFIG(config->frame_clk_freq),
  .slot_cfg=I2S_PDM_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,I2S_SLOT_MODE_MONO),
  .gpio_cfg={.clk=FP_MIC_CLOCK,.din=FP_MIC_DATA}};
 if(status==ESP_OK)status=i2s_channel_init_pdm_rx_mode(rx,&pdm);
 if(status!=ESP_OK){if(rx)i2s_del_channel(rx);rx=NULL;return error(status);}
 rx_config=*config;return 0;
}
int i2s_trigger(const struct device *d,enum i2s_dir dir,int trigger){
 (void)d;
 if(trigger==I2S_TRIGGER_DRAIN){if(tx_on)k_msleep(8*128*1000/physical_rate+2);return 0;}
 if(trigger==I2S_TRIGGER_DROP||trigger==I2S_TRIGGER_PREPARE){
  if((dir&I2S_DIR_TX)&&tx_on){i2s_channel_disable(tx);tx_on=false;}
  if((dir&I2S_DIR_RX)&&rx_on){i2s_channel_disable(rx);rx_on=false;}return 0;
 }
 if(trigger!=I2S_TRIGGER_START)return -EINVAL;
 esp_err_t status=ESP_OK;
 if((dir&I2S_DIR_TX)&&!tx_on){status=tx?i2s_channel_enable(tx):ESP_ERR_INVALID_STATE;if(status==ESP_OK)tx_on=true;}
 if(status==ESP_OK&&(dir&I2S_DIR_RX)&&!rx_on){status=rx?i2s_channel_enable(rx):ESP_ERR_INVALID_STATE;if(status==ESP_OK)rx_on=true;}
 return error(status);
}
int i2s_write(const struct device *d,void *block,size_t size){
 (void)d;if(!tx||!converted||size%4||size/4*ratio*8>converted_capacity)return -EINVAL;
 const int16_t *input=block;size_t n=0;
 for(size_t i=0;i<size/4;i++){
  if(!have_previous){previous[0]=input[i*2];previous[1]=input[i*2+1];have_previous=true;}
  for(unsigned j=1;j<=ratio;j++)for(unsigned ch=0;ch<2;ch++){
   int value=previous[ch]+((int32_t)input[i*2+ch]-previous[ch])*(int)j/(int)ratio;
   converted[n++]=(int32_t)value*65536;
  }
  previous[0]=input[i*2];previous[1]=input[i*2+1];
 }
 size_t bytes=n*sizeof(int32_t),wrote=0;
 esp_err_t status=tx_on?i2s_channel_write(tx,converted,bytes,&wrote,tx_config.timeout):i2s_channel_preload_data(tx,converted,bytes,&wrote);
 if(status!=ESP_OK)return error(status);
 if(wrote!=bytes)return -EIO;
 /* Start after one complete block; never overfill DMA while preloading. */
 if(!tx_on){status=i2s_channel_enable(tx);if(status!=ESP_OK)return error(status);tx_on=true;}
 k_mem_slab_free(tx_config.mem_slab,block);return 0;
}
int i2s_read(const struct device *d,void **block,size_t *size){
 (void)d;if(!rx_on||!rx_config.mem_slab)return -ENODEV;
 int r=k_mem_slab_alloc(rx_config.mem_slab,block,pdMS_TO_TICKS(rx_config.timeout));if(r)return r;
 size_t got=0;esp_err_t status=i2s_channel_read(rx,*block,rx_config.block_size/2,&got,rx_config.timeout);
 if(status!=ESP_OK||got%2){k_mem_slab_free(rx_config.mem_slab,*block);*block=NULL;return status==ESP_OK?-EIO:error(status);}
 int16_t *pcm=*block;for(size_t i=got/2;i>0;i--){pcm[(i-1)*2]=pcm[i-1];pcm[(i-1)*2+1]=pcm[i-1];}
 *size=got*2;return 0;
}

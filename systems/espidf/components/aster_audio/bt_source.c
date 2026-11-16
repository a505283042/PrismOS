/* SPDX-License-Identifier: MIT */
#include "bt_source.h"
#include "aster_audio_output.h"
#include "aster_rate_convert.h"
#include "aster_platform.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include <stdatomic.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
static StreamBufferHandle_t queue;
static atomic_bool linked,streaming,active,reset_queue;
static atomic_uint output_rate,queued,underruns;
static aster_audio_format_t input_format;
static aster_rate_convert_t *converter;
static int16_t *converted;
static uint64_t submitted;
static bool tail_sent;
void aster_bt_tx_prepare(StreamBufferHandle_t next){queue=next;atomic_store(&active,false);atomic_store(&reset_queue,false);atomic_store(&linked,false);atomic_store(&streaming,false);atomic_store(&queued,0);atomic_store(&underruns,0);}
void aster_bt_tx_link(bool up,bool stream,uint32_t rate){atomic_store(&output_rate,rate);atomic_store(&linked,up);atomic_store(&streaming,stream);}
bool aster_bt_tx_active(void){return atomic_load(&active);}
uint32_t aster_bt_tx_queued(void){return atomic_load(&queued);}
int32_t aster_bt_tx_callback(uint8_t *data,int32_t length){
 if(!data || length<=0 || length%4)return 0;
 memset(data,0,(size_t)length);
 /* This callback is the sole reader, including discarding old audio after a
  * seek/reopen. The producer never resets a stream under the consumer. */
 if(atomic_load(&reset_queue)){
  while(xStreamBufferReceive(queue,data,(size_t)length,0)){}
  memset(data,0,(size_t)length);atomic_store(&queued,0);atomic_store(&reset_queue,false);return length;
 }
 if(!atomic_load(&active))return length;
 size_t n=xStreamBufferReceive(queue,data,(size_t)length,0);
 if(n<(size_t)length){memset(data+n,0,(size_t)length-n);atomic_fetch_add(&underruns,1);}
 atomic_store(&queued,xStreamBufferBytesAvailable(queue));return length;
}
static int caps(void *ctx,aster_audio_output_caps_t *out){
 (void)ctx;out->formats=ASTER_AUDIO_PCM_BIT(ASTER_AUDIO_PCM_S32LE);out->rates=31;
 out->channels=ASTER_AUDIO_CHANNEL_BIT(2);out->flags=ASTER_AUDIO_OUTPUT_EXCLUSIVE;out->max_write_frames=512;return 0;
}
static int open_output(void *ctx,const aster_audio_format_t *format){
 (void)ctx;if(!atomic_load(&linked) || !atomic_load(&streaming))return -ENOTCONN;
 if(converter || converted)return -EBUSY;
 converter=heap_caps_malloc(sizeof(*converter),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
 converted=heap_caps_malloc(4096,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
 int r=converter && converted?aster_rate_convert_init(converter,format->rate,atomic_load(&output_rate)):-ENOMEM;
 if(!r){
  atomic_store(&reset_queue,true);int64_t deadline=esp_timer_get_time()+750000;
  while(atomic_load(&reset_queue) && atomic_load(&streaming) && esp_timer_get_time()<deadline)vTaskDelay(pdMS_TO_TICKS(2));
  if(atomic_load(&reset_queue))r=-ETIMEDOUT;
 }
 if(r){free(converter);free(converted);converter=NULL;converted=NULL;return r;}
 input_format=*format;submitted=0;tail_sent=false;atomic_store(&active,true);return 0;
}
static int write_output(void *ctx,const void *pcm,uint32_t frames){
 (void)ctx;if(!atomic_load(&linked) || !atomic_load(&streaming))return -ENOTCONN;
 if(!converter || !atomic_load(&active))return -EBADF;
 size_t bound=aster_rate_convert_bound(converter,frames);
 if(bound>1024)return -EMSGSIZE;
 if(xStreamBufferSpacesAvailable(queue)<bound*4)return -EAGAIN;
 int n=aster_rate_convert_process(converter,pcm,frames,converted,1024);if(n<0)return n;
 int volume=aster_plat_audio_get_volume();if(volume<0)volume=0;if(volume>100)volume=100;
 for(int i=0;i<n*2;i++)converted[i]=(int16_t)((int32_t)converted[i]*volume/100);
 if(n && xStreamBufferSend(queue,converted,(size_t)n*4,0)!=(size_t)n*4)return -EIO;
 atomic_store(&queued,xStreamBufferBytesAvailable(queue));submitted+=frames;tail_sent=false;return (int)frames;
}
static int drain_output(void *ctx){
 (void)ctx;if(!converter)return -EBADF;
 int64_t deadline=esp_timer_get_time()+1500000;
 if(!tail_sent && converter->input_rate!=converter->output_rate){
  const int32_t silence[64]={0};int r;
  do {r=write_output(NULL,silence,32);if(r==-EAGAIN)vTaskDelay(pdMS_TO_TICKS(2));}while(r==-EAGAIN && esp_timer_get_time()<deadline);
  if(r<0)return r;
  submitted-=32;tail_sent=true;
 }
 while(atomic_load(&queued) && atomic_load(&linked) && atomic_load(&streaming) && esp_timer_get_time()<deadline)vTaskDelay(pdMS_TO_TICKS(2));
 if(!atomic_load(&linked))return -ENOTCONN;
 return atomic_load(&queued)?-ETIMEDOUT:0;
}
static int close_output(void *ctx){
 (void)ctx;atomic_store(&active,false);free(converter);free(converted);converter=NULL;converted=NULL;return 0;
}
static int stats(void *ctx,aster_audio_output_stats_t *out){
 (void)ctx;out->rate=input_format.rate;out->channels=2;out->format=ASTER_AUDIO_PCM_S32LE;out->submitted_frames=submitted;
 out->underruns=atomic_load(&underruns);return 0;
}
int aster_bt_tx_register(void){
 static const aster_audio_output_ops_t ops={.size=sizeof(ops),.version=1,.name="Classic A2DP / SBC",
  .caps=caps,.open=open_output,.write=write_output,.drain=drain_output,.close=close_output,.stats=stats};
 return aster_audio_output_bind(ASTER_AUDIO_OUTPUT_BLUETOOTH,&ops,NULL);
}

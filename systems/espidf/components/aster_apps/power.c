/* SPDX-License-Identifier: MIT */
#include "aster_idf_apps.h"
#include "aster_idf_runtime.h"
#include "aster_power_session.h"
#include "aster_power_ui.h"
#include "aster_storage.h"
#include "aster_app_mgr.h"
#include "font_sd.h"
#include "aster_os.h"
#include "aster_device.h"
#include "aster_media.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <errno.h>
#include <stdio.h>
static int caps(void *ctx,aster_power_caps_t *out)
{int r=aster_power_device_base.caps(ctx,out);if(!r)out->modes|=ASTER_POWER_RESTART;return r;}
static int screen(void *ctx,bool on,uint8_t level){return aster_power_device_base.screen(ctx,on,level);}
static int restart(void *ctx){(void)ctx;esp_restart();return -EIO;}
const aster_power_ops_t aster_idf_power_base={.size=sizeof(aster_power_ops_t),.version=ASTER_POWER_API_VERSION,
 .name="esp-idf-system",.caps=caps,.screen=screen,.restart=restart};
static int audio(void *ctx,bool sleeping)
{
 (void)ctx;
#if CONFIG_ASTER_BOARD_FAKEPOD_S31
 extern int aster_idf_bt_audio_shutdown(void);extern bool aster_idf_bt_audio_busy(void);
 aster_media_snapshot_t state={0};
 int snapshot=aster_media_snapshot(&state);
 bool media=!snapshot&&(state.state==ASTER_MEDIA_OPENING||state.state==ASTER_MEDIA_PLAYING||state.state==ASTER_MEDIA_PAUSED||state.state==ASTER_MEDIA_STOPPING);
 if(sleeping&&(media||aster_idf_bt_audio_busy()))return -EBUSY;
 (void)aster_media_stop();
 uint64_t until=aster_os_uptime_ms()+5000;
 while(media){
  aster_os_sleep_ms(10);(void)aster_media_snapshot(&state);
  media=state.state==ASTER_MEDIA_OPENING||state.state==ASTER_MEDIA_PLAYING||state.state==ASTER_MEDIA_PAUSED||state.state==ASTER_MEDIA_STOPPING;
  if(media&&aster_os_uptime_ms()>=until)return -ETIMEDOUT;
 }
 int r=aster_idf_bt_audio_shutdown();if(r)return r;
#else
 (void)sleeping;
#endif
 aster_device_audio_ui_stop();return aster_device_has(ASTER_DEVICE_METHOD_audio_stop)?aster_device_audio_stop():0;
}
static bool wifi_stopped,wifi_connected;
static int network(void *ctx,bool suspend)
{
 (void)ctx;
 if(suspend){
  wifi_ap_record_t ap;wifi_connected=esp_wifi_sta_get_ap_info(&ap)==ESP_OK;
  esp_err_t r=esp_wifi_stop();
  if(r==ESP_ERR_WIFI_NOT_INIT||r==ESP_ERR_WIFI_NOT_STARTED)return 0;
  if(r)return -EIO;
  wifi_stopped=true;return 0;
 }
 if(!wifi_stopped)return 0;
 wifi_stopped=false;
 if(esp_wifi_start()!=ESP_OK)return -EIO;
 if(wifi_connected)(void)esp_wifi_connect();
 return 0;
}
static int storage(void *ctx,bool suspend)
{
 (void)ctx;printf("POWER storage=%s begin\n",suspend?"suspend":"resume");
 int result=aster_storage_power(suspend);
 printf("POWER storage=%s result=%d\n",suspend?"suspend":"resume",result);return result;
}
static void ui(void *ctx,const aster_power_request_t *request,unsigned stage,int result)
{
 (void)ctx;
 if(stage==4 && request->mode==ASTER_POWER_USB_STORAGE && aster_storage_mounted()){
  aster_app_mgr_scan();
  aster_font_sd_reload();
 }
 printf("POWER stage=%u mode=%u result=%d active=%u\n",stage,(unsigned)request->mode,
        result,aster_power_activity_count());
 aster_power_ui_update(request,stage,result);
}
static void stop(void *ctx){(void)ctx;aster_idf_request_stop();}
static bool busy(void *ctx){(void)ctx;return aster_idf_apps_busy();}
void aster_idf_power_register(void)
{
 static const aster_power_session_ops_t callbacks={audio,network,storage,ui};
 static aster_power_session_t session={.ops=&callbacks};
 static const aster_power_lifecycle_t life={.size=sizeof(life),.version=ASTER_POWER_API_VERSION,
  .name="idf-lifecycle",.prepare=aster_power_session_prepare,.finish=aster_power_session_finish,.recovery_error=aster_power_session_error};
 static const aster_power_dispatch_t dispatch={sizeof(dispatch),1,"idf-worker",stop,busy};
 if(!aster_power_ops())(void)aster_power_bind(&aster_idf_power_base,NULL);
 (void)aster_power_bind_lifecycle(&life,&session);(void)aster_power_bind_dispatch(&dispatch,NULL);
}

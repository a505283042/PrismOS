/* SPDX-License-Identifier: MIT */
#include "aster_power.h"
#include "aster_power_session.h"
#include "aster_power_ui.h"
#include "aster_device.h"
#include "aster_storage.h"
#include "aster_runtime.h"
#include <zephyr/sys/reboot.h>
#include <errno.h>
static int caps(void *ctx,aster_power_caps_t *out)
{int r=aster_power_device_base.caps(ctx,out);if(!r)out->modes|=ASTER_POWER_RESTART;return r;}
static int screen(void *ctx,bool on,uint8_t level){return aster_power_device_base.screen(ctx,on,level);}
static int restart(void *ctx){(void)ctx;sys_reboot(SYS_REBOOT_COLD);return -EIO;}
static int audio(void *ctx,bool sleeping){(void)ctx;(void)sleeping;aster_device_audio_ui_stop();return aster_device_has(ASTER_DEVICE_METHOD_audio_stop)?aster_device_audio_stop():0;}
static int storage(void *ctx,bool suspend){(void)ctx;return aster_storage_power(suspend);}
static void ui(void *ctx,const aster_power_request_t *r,unsigned stage,int result)
{
 (void)ctx;
#if CONFIG_ASTER_LVGL
 aster_power_ui_update(r,stage,result);
#else
 (void)r;(void)stage;(void)result;
#endif
}
static void stop(void *ctx){(void)ctx;/* Runtime observes the common pending flag. */}
static bool busy(void *ctx)
{
 (void)ctx;
#if CONFIG_ASTER_WAMR
 return aster_runtime_power_busy();
#else
 return false;
#endif
}
void aster_zephyr_power_register(void)
{
 static const aster_power_ops_t device={.size=sizeof(device),.version=ASTER_POWER_API_VERSION,.name="zephyr-system",.caps=caps,.screen=screen,.restart=restart};
 static const aster_power_session_ops_t callbacks={.audio=audio,.storage=storage,.ui=ui};
 static aster_power_session_t session={.ops=&callbacks};
 static const aster_power_lifecycle_t life={.size=sizeof(life),.version=ASTER_POWER_API_VERSION,
 .name="zephyr-lifecycle",.prepare=aster_power_session_prepare,.finish=aster_power_session_finish,.recovery_error=aster_power_session_error};
 static const aster_power_dispatch_t dispatch={sizeof(dispatch),1,"zephyr-worker",stop,busy};
 (void)aster_power_bind(&device,NULL);(void)aster_power_bind_lifecycle(&life,&session);(void)aster_power_bind_dispatch(&dispatch,NULL);aster_power_seal();
}

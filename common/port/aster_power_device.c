/* SPDX-License-Identifier: MIT */
#include "aster_power.h"
#include "aster_device.h"
#include "aster_os.h"
#include <errno.h>
static int caps(void *context,aster_power_caps_t *out)
{
 (void)context;
 *out=(aster_power_caps_t){.size=sizeof(*out),.version=ASTER_POWER_API_VERSION};
 if(aster_device_has(ASTER_DEVICE_METHOD_backlight_set)){
  if(aster_device_input_touch_available())out->screen_wake|=ASTER_WAKE_TOUCH;
  if(aster_device_input_keys_available())out->screen_wake|=ASTER_WAKE_KEY;
  if(out->screen_wake)out->modes=ASTER_POWER_SCREEN_OFF;
 }
 out->keys=aster_device_input_keys_available();return 0;
}
static int screen(void *context,bool on,uint8_t brightness)
{(void)context;return aster_device_backlight_set(on,brightness)?-EIO:0;}
static int keys(void *context,aster_power_key_t *out,uint32_t capacity)
{
 (void)context;aster_key_evt_t events[ASTER_PLAT_MAX_KEYS];
 int n=aster_device_input_keys_poll(events,capacity<ASTER_PLAT_MAX_KEYS?(int)capacity:ASTER_PLAT_MAX_KEYS);
 if(n<0)return -EIO;
 if(n>ASTER_PLAT_MAX_KEYS)return -EPROTO;
 uint64_t now=aster_os_uptime_ms();
 for(int i=0;i<n;i++)out[i]=(aster_power_key_t){events[i].code,events[i].pressed,now};
 return n;
}
const aster_power_ops_t aster_power_device_base={
 .size=sizeof(aster_power_ops_t),.version=ASTER_POWER_API_VERSION,.name="device-default",
 .caps=caps,.screen=screen,.keys=keys
};
void aster_power_default_init(void)
{if(!aster_power_ops())(void)aster_power_bind(&aster_power_device_base,NULL);}

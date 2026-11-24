"""Compile the real optional power registry and exercise lifecycle/idle policy."""
import unittest
from test_waveshare import ROOT, run_c

class Power(unittest.TestCase):
    def test_overrides_quiescence_and_idle_wake(self):
        header=(ROOT/'common/port/aster_power.h').read_text()
        source=(ROOT/'common/port/aster_power.c').read_text().replace('#include "aster_power.h"','')
        run_c(header+source+r"""
#include <assert.h>
static int prepare_error,screen_error,battery_error;
static unsigned prepared,finished,suspended,screen_calls,brightness_value;
static bool screen_on;
static int caps(void *ctx,aster_power_caps_t *out){
 assert(ctx==(void *)1);out->modes=31;out->screen_wake=ASTER_WAKE_TOUCH|ASTER_WAKE_KEY;
 out->light_wake=ASTER_WAKE_KEY|ASTER_WAKE_TIMER;out->deep_wake=ASTER_WAKE_TIMER;
 out->battery=true;out->keys=true;return 0;
}
static int screen(void *ctx,bool on,uint8_t brightness){
 assert(ctx==(void *)1);screen_calls++;screen_on=on;brightness_value=brightness;return screen_error;
}
static int battery(void *ctx,aster_power_battery_t *out){
 (void)ctx;out->flags=ASTER_POWER_BATTERY_PRESENT|ASTER_POWER_PERCENT_VALID;
 out->percent=battery_error?101:73;return 0;
}
static int suspend_driver(void *ctx,const aster_power_request_t *r){
 (void)ctx;suspended++;assert(r->mode==ASTER_POWER_LIGHT_SLEEP);return -EIO;
}
static int prepare(void *ctx,const aster_power_request_t *r){
 assert(ctx==(void *)2);prepared++;
 assert(aster_power_transition(r,80)==-EBUSY);return prepare_error;
}
static void finish(void *ctx,const aster_power_request_t *r,int result){
 assert(ctx==(void *)2);assert(r->mode==ASTER_POWER_LIGHT_SLEEP);assert(result==-EIO);finished++;
}
int main(void){
 aster_power_caps_t c;assert(aster_power_caps(&c)==-ENOTSUP&&c.modes==0);
 aster_power_battery_t b;assert(aster_power_battery(&b)==-ENOTSUP&&b.flags==0);
 aster_power_ops_t base={sizeof(base),ASTER_POWER_API_VERSION,"base",.caps=caps,.screen=screen,.suspend=suspend_driver};
 aster_power_ops_t over={sizeof(over),ASTER_POWER_API_VERSION,"board",.battery=battery},derived;
 assert(!aster_power_derive(&derived,&base,&over));assert(derived.screen==screen&&derived.battery==battery);
 assert(!aster_power_derive(&derived,&derived,&over));assert(!aster_power_bind(&derived,(void *)1));
 assert(!aster_power_caps(&c)&&c.modes==ASTER_POWER_SCREEN_OFF&&c.battery&&!c.keys);
 assert(!aster_power_battery(&b)&&b.percent==73);
 battery_error=1;assert(aster_power_battery(&b)==-EPROTO&&b.flags==0);battery_error=0;
 aster_power_request_t r={sizeof(r),ASTER_POWER_API_VERSION,ASTER_POWER_LIGHT_SLEEP,ASTER_WAKE_KEY,0};
 assert(aster_power_transition(&r,80)==-ENOTSUP&&!suspended);
 aster_power_lifecycle_t life={.size=sizeof(life),.version=ASTER_POWER_API_VERSION,.name="os",.prepare=prepare,.finish=finish},life_over={.size=sizeof(life),.version=ASTER_POWER_API_VERSION,.name="custom"},life_derived;
 assert(!aster_power_lifecycle_derive(&life_derived,&life,&life_over));assert(life_derived.prepare==prepare);
 assert(!aster_power_bind_lifecycle(&life_derived,(void *)2));
 r.wake_sources=ASTER_WAKE_TOUCH;assert(aster_power_transition(&r,80)==-ENOTSUP&&!prepared);
 r.wake_sources=ASTER_WAKE_KEY;prepare_error=-EBUSY;
 assert(aster_power_transition(&r,80)==-EBUSY&&prepared==1&&!finished&&!suspended);
 prepare_error=0;assert(aster_power_transition(&r,80)==-EIO&&prepared==2&&finished==1&&suspended==1);
 r.wake_sources=ASTER_WAKE_TIMER;assert(aster_power_transition(&r,80)==-EINVAL);
 r.mode=ASTER_POWER_LIGHT_SLEEP|ASTER_POWER_SCREEN_OFF;assert(aster_power_transition(&r,80)==-EINVAL);
 aster_power_idle_t idle;aster_power_idle_init(&idle,100,1000,180);
 assert(!aster_power_idle_tick(&idle,1100,true)&&!idle.screen_off);
 assert(!aster_power_idle_tick(&idle,2100,false)&&idle.screen_off&&!screen_on&&brightness_value==180);
 unsigned calls=screen_calls;assert(!aster_power_idle_tick(&idle,3100,false)&&screen_calls==calls);
 assert(aster_power_idle_activity(&idle,3200,true)&&screen_on&&!idle.screen_off);
 assert(aster_power_idle_activity(&idle,3300,true));assert(aster_power_idle_activity(&idle,3400,false));
 assert(!aster_power_idle_activity(&idle,3500,true));
 screen_error=-EIO;assert(aster_power_idle_tick(&idle,4500,false)==-EIO&&!idle.screen_off);
 aster_power_seal();assert(aster_power_bind(&base,0)==-EBUSY);assert(aster_power_bind_lifecycle(&life,0)==-EBUSY);
 return 0;
}
""")

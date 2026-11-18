/* SPDX-License-Identifier: MIT */
#include "aster_power.h"
#include <errno.h>
#include <string.h>
#include <stdatomic.h>
static const aster_power_ops_t *driver;
static const aster_power_lifecycle_t *lifecycle;
static void *driver_context,*lifecycle_context;
static bool sealed,transitioning;
const aster_power_ops_t *aster_power_ops(void){return driver;}
static bool valid(const aster_power_ops_t *p)
{return p&&p->size==sizeof(*p)&&p->version==ASTER_POWER_API_VERSION&&p->name&&p->caps;}
static bool valid_lifecycle(const aster_power_lifecycle_t *p)
{return p&&p->size==sizeof(*p)&&p->version==ASTER_POWER_API_VERSION&&p->name;}
int aster_power_derive(aster_power_ops_t *out,const aster_power_ops_t *base,const aster_power_ops_t *overrides)
{
 if(!out||!valid(base)||!overrides||overrides->size!=sizeof(*overrides)||overrides->version!=ASTER_POWER_API_VERSION||!overrides->name)return -EINVAL;
 aster_power_ops_t merged=*base;merged.name=overrides->name;
#define OVERRIDE(member) if(overrides->member)merged.member=overrides->member
 OVERRIDE(caps);OVERRIDE(battery);OVERRIDE(screen);OVERRIDE(suspend);
 OVERRIDE(restart);OVERRIDE(shutdown);OVERRIDE(keys);OVERRIDE(map_key);
#undef OVERRIDE
 *out=merged;return 0;
}
int aster_power_lifecycle_derive(aster_power_lifecycle_t *out,const aster_power_lifecycle_t *base,const aster_power_lifecycle_t *overrides)
{
 if(!out||!valid_lifecycle(base)||!valid_lifecycle(overrides))return -EINVAL;
 aster_power_lifecycle_t merged=*base;merged.name=overrides->name;
 if(overrides->prepare)merged.prepare=overrides->prepare;
 if(overrides->finish)merged.finish=overrides->finish;
 if(overrides->recovery_error)merged.recovery_error=overrides->recovery_error;
 *out=merged;return 0;
}
int aster_power_bind(const aster_power_ops_t *ops,void *context)
{if(sealed)return -EBUSY;if(!valid(ops))return -EINVAL;driver=ops;driver_context=context;return 0;}
int aster_power_bind_lifecycle(const aster_power_lifecycle_t *ops,void *context)
{if(sealed)return -EBUSY;if(!valid_lifecycle(ops))return -EINVAL;lifecycle=ops;lifecycle_context=context;return 0;}
void aster_power_seal(void){sealed=true;}
int aster_power_caps(aster_power_caps_t *out)
{
 if(!out)return -EINVAL;
 *out=(aster_power_caps_t){.size=sizeof(*out),.version=ASTER_POWER_API_VERSION};
 if(!driver)return -ENOTSUP;
 aster_power_caps_t value=*out;int r=driver->caps(driver_context,&value);if(r)return r;
 if(value.size!=sizeof(value)||value.version!=ASTER_POWER_API_VERSION)return -EPROTO;
 value.modes&=ASTER_POWER_SCREEN_OFF|ASTER_POWER_LIGHT_SLEEP|ASTER_POWER_DEEP_SLEEP|ASTER_POWER_RESTART|ASTER_POWER_SHUTDOWN;
 if(!driver->screen)value.modes&=~ASTER_POWER_SCREEN_OFF;
 if(!driver->suspend)value.modes&=~(ASTER_POWER_LIGHT_SLEEP|ASTER_POWER_DEEP_SLEEP);
 if(!driver->restart)value.modes&=~ASTER_POWER_RESTART;
 if(!driver->shutdown)value.modes&=~ASTER_POWER_SHUTDOWN;
 if(!lifecycle||!lifecycle->prepare||!lifecycle->finish)value.modes&=ASTER_POWER_SCREEN_OFF;
 value.battery=value.battery&&driver->battery;
 value.keys=value.keys&&driver->keys;
 if(!(value.modes&ASTER_POWER_SCREEN_OFF))value.screen_wake=0;
 if(!(value.modes&ASTER_POWER_LIGHT_SLEEP))value.light_wake=0;
 if(!(value.modes&ASTER_POWER_DEEP_SLEEP))value.deep_wake=0;
 *out=value;return 0;
}
int aster_power_battery(aster_power_battery_t *out)
{
 if(!out)return -EINVAL;
 *out=(aster_power_battery_t){.size=sizeof(*out),.version=ASTER_POWER_API_VERSION};
 aster_power_caps_t caps;int r=aster_power_caps(&caps);if(r)return r;if(!caps.battery)return -ENOTSUP;
 aster_power_battery_t value=*out;r=driver->battery(driver_context,&value);if(r)return r;
 if(value.size!=sizeof(value)||value.version!=ASTER_POWER_API_VERSION)return -EPROTO;
 if((value.flags&ASTER_POWER_PERCENT_VALID)&&(!(value.flags&ASTER_POWER_BATTERY_PRESENT)||value.percent>100))return -EPROTO;
 if((value.flags&ASTER_POWER_VOLTAGE_VALID)&&!(value.flags&ASTER_POWER_BATTERY_PRESENT))return -EPROTO;
 *out=value;return 0;
}
int aster_power_keys(aster_power_key_t *events,uint32_t capacity)
{
 if(!events||!capacity||capacity>32)return -EINVAL;
 aster_power_caps_t caps;int r=aster_power_caps(&caps);if(r)return r;if(!caps.keys)return -ENOTSUP;
 r=driver->keys(driver_context,events,capacity);return r>(int)capacity?-EPROTO:r;
}
aster_power_key_action_t aster_power_map_key(uint32_t code,bool pressed,uint32_t held_ms)
{return driver&&driver->map_key?driver->map_key(driver_context,code,pressed,held_ms):ASTER_KEY_ACTION_NONE;}
int aster_power_screen_on(uint8_t brightness)
{return driver&&driver->screen?driver->screen(driver_context,true,brightness):-ENOTSUP;}
int aster_power_transition(const aster_power_request_t *request,uint8_t brightness)
{
 if(!request||request->size!=sizeof(*request)||request->version!=ASTER_POWER_API_VERSION||!request->mode||(request->mode&(request->mode-1))||
    (request->mode&~(ASTER_POWER_SCREEN_OFF|ASTER_POWER_LIGHT_SLEEP|ASTER_POWER_DEEP_SLEEP|ASTER_POWER_RESTART|ASTER_POWER_SHUTDOWN)))return -EINVAL;
 if(transitioning)return -EBUSY;
 aster_power_caps_t caps;int r=aster_power_caps(&caps);if(r)return r;
 if(!(caps.modes&request->mode))return -ENOTSUP;
 uint32_t allowed=request->mode==ASTER_POWER_SCREEN_OFF?caps.screen_wake:request->mode==ASTER_POWER_LIGHT_SLEEP?caps.light_wake:request->mode==ASTER_POWER_DEEP_SLEEP?caps.deep_wake:0;
 if(request->mode&(ASTER_POWER_SCREEN_OFF|ASTER_POWER_LIGHT_SLEEP|ASTER_POWER_DEEP_SLEEP)){
  if(!request->wake_sources||(request->wake_sources&~allowed))return -ENOTSUP;
 }else if(request->wake_sources||request->timer_ms)return -EINVAL;
 if(((request->wake_sources&ASTER_WAKE_TIMER)!=0)!=(request->timer_ms!=0))return -EINVAL;
 /* Freeze the caller's request before callbacks, including nested calls. */
 aster_power_request_t frozen=*request;transitioning=true;
 if(frozen.mode==ASTER_POWER_SCREEN_OFF)r=driver->screen(driver_context,false,brightness);
 else{
  r=lifecycle->prepare(lifecycle_context,&frozen);
  if(!r){
   if(frozen.mode==ASTER_POWER_RESTART)r=driver->restart(driver_context);
   else if(frozen.mode==ASTER_POWER_SHUTDOWN)r=driver->shutdown(driver_context);
   else r=driver->suspend(driver_context,&frozen);
   lifecycle->finish(lifecycle_context,&frozen,r);
   if(lifecycle->recovery_error){int recovery=lifecycle->recovery_error(lifecycle_context);if(recovery)r=recovery;}
  }
 }
 transitioning=false;return r;
}
void aster_power_idle_init(aster_power_idle_t *s,uint64_t now,uint32_t timeout_ms,uint8_t brightness)
{if(s)*s=(aster_power_idle_t){.last_activity_ms=now,.timeout_ms=timeout_ms,.brightness=brightness,.initialized=true};}
int aster_power_idle_tick(aster_power_idle_t *s,uint64_t now,bool inhibited)
{
 if(!s||!s->initialized)return -EINVAL;
 if(now<s->last_activity_ms||inhibited){s->last_activity_ms=now;return 0;}
 if(s->screen_off||!s->timeout_ms||now-s->last_activity_ms<s->timeout_ms)return 0;
 aster_power_caps_t caps;int r=aster_power_caps(&caps);if(r)return r;
 uint32_t wake=caps.screen_wake&(ASTER_WAKE_TOUCH|ASTER_WAKE_KEY|ASTER_WAKE_EXTERNAL);
 if(!wake)return -ENOTSUP;
 aster_power_request_t request={sizeof(request),ASTER_POWER_API_VERSION,ASTER_POWER_SCREEN_OFF,wake,0};
 r=aster_power_transition(&request,s->brightness);if(!r)s->screen_off=true;return r;
}
bool aster_power_idle_activity(aster_power_idle_t *s,uint64_t now,bool contacts_down)
{
 if(!s||!s->initialized)return false;
 s->last_activity_ms=now;
 if(s->screen_off){if(aster_power_screen_on(s->brightness))return true;s->screen_off=false;s->wake_gesture=true;}
 bool consumed=s->wake_gesture;if(!contacts_down)s->wake_gesture=false;return consumed;
}

#define ADMISSION_CLOSED (1u << 31)
static atomic_uint activity;
int aster_power_activity_begin(void)
{
 unsigned value=atomic_load(&activity);
 for(;;){
  if(value&ADMISSION_CLOSED)return -EBUSY;
  if(value==ADMISSION_CLOSED-1)return -EOVERFLOW;
  if(atomic_compare_exchange_weak(&activity,&value,value+1))return 0;
 }
}
void aster_power_activity_end(void)
{
 unsigned value=atomic_load(&activity);
 while((value&~ADMISSION_CLOSED)&&!atomic_compare_exchange_weak(&activity,&value,value-1)){}
}
bool aster_power_activity_closing(void){return (atomic_load(&activity)&ADMISSION_CLOSED)!=0;}
unsigned aster_power_activity_count(void){return atomic_load(&activity)&~ADMISSION_CLOSED;}
int aster_power_activity_close(void)
{return (atomic_fetch_or(&activity,ADMISSION_CLOSED)&ADMISSION_CLOSED)?-EBUSY:0;}
void aster_power_activity_reopen(void){atomic_fetch_and(&activity,~ADMISSION_CLOSED);}
static const aster_power_dispatch_t *dispatch;
static void *dispatch_context;
static atomic_uint mailbox;
static atomic_int last_result;
static aster_power_request_t queued_request;
int aster_power_bind_dispatch(const aster_power_dispatch_t *ops,void *context)
{
 if(sealed)return -EBUSY;
 if(!ops||ops->size!=sizeof(*ops)||ops->version!=1||!ops->name||!ops->stop_app||!ops->app_busy)return -EINVAL;
 dispatch=ops;dispatch_context=context;return 0;
}
int aster_power_schedule(const aster_power_request_t *request)
{
 if(!request||request->size!=sizeof(*request)||request->version!=ASTER_POWER_API_VERSION||
    !request->mode||(request->mode&(request->mode-1)))return -EINVAL;
 if(!dispatch||request->mode==ASTER_POWER_SCREEN_OFF)return -ENOTSUP;
 aster_power_caps_t caps;int r=aster_power_caps(&caps);if(r)return r;
 if(!(caps.modes&request->mode))return -ENOTSUP;
 uint32_t allowed=request->mode==ASTER_POWER_LIGHT_SLEEP?caps.light_wake:request->mode==ASTER_POWER_DEEP_SLEEP?caps.deep_wake:0;
 if(request->wake_sources&~allowed)return -ENOTSUP;
 if((request->mode&(ASTER_POWER_LIGHT_SLEEP|ASTER_POWER_DEEP_SLEEP))&&!request->wake_sources)return -EINVAL;
 if(((request->wake_sources&ASTER_WAKE_TIMER)!=0)!=(request->timer_ms!=0))return -EINVAL;
 unsigned empty=0;if(!atomic_compare_exchange_strong(&mailbox,&empty,1))return -EBUSY;
 queued_request=*request;atomic_store(&last_result,-EINPROGRESS);
 dispatch->stop_app(dispatch_context);
 atomic_store_explicit(&mailbox,2,memory_order_release);return 0;
}
bool aster_power_pending(void){return atomic_load(&mailbox)!=0;}
int aster_power_last_result(void){return atomic_load(&last_result);}
void aster_power_poll(void)
{
 if(atomic_load_explicit(&mailbox,memory_order_acquire)!=2||!dispatch||dispatch->app_busy(dispatch_context))return;
 unsigned ready=2;if(!atomic_compare_exchange_strong(&mailbox,&ready,3))return;
 int result=aster_power_transition(&queued_request,230);
 atomic_store(&last_result,result);atomic_store(&mailbox,0);
}

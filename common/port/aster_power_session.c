/* SPDX-License-Identifier: MIT */
#include "aster_power_session.h"
#include "aster_os.h"
#include <errno.h>
static void ui(aster_power_session_t *s,const aster_power_request_t *r,unsigned stage,int result)
{if(s->ops->ui)s->ops->ui(s->context,r,stage,result);}
void aster_power_session_finish(void *context,const aster_power_request_t *request,int result)
{
 aster_power_session_t *s=context;int error=0,r;
 if(s->storage){r=s->ops->storage(s->context,false);if(r)error=r;s->storage=false;}
 if(s->network){r=s->ops->network(s->context,false);if(r&&!error)error=r;s->network=false;}
 if(s->admission){aster_power_activity_reopen();s->admission=false;}
 s->recovery_error=error;
 ui(s,request,4,error?error:result);
}
int aster_power_session_prepare(void *context,const aster_power_request_t *request)
{
 aster_power_session_t *s=context;
 if(!s||!s->ops||!s->ops->audio||!s->ops->storage)return -ENOTSUP;
 bool sleeping=request->mode&(ASTER_POWER_LIGHT_SLEEP|ASTER_POWER_DEEP_SLEEP);
 int r=aster_power_activity_close();if(r)return r;s->admission=true;s->recovery_error=0;
 ui(s,request,0,0);
 r=s->ops->audio(s->context,sleeping);if(r)goto fail;
 ui(s,request,1,0);
 uint64_t until=aster_os_uptime_ms()+8000;
 while(aster_power_activity_count()){
  if(aster_os_uptime_ms()>=until){r=-EBUSY;goto fail;}
  aster_os_sleep_ms(10);
 }
 if(s->ops->network){r=s->ops->network(s->context,true);if(r)goto fail;s->network=true;}
 ui(s,request,2,0);
 r=s->ops->storage(s->context,true);if(r)goto fail;s->storage=true;
 /* No UI/font/file reads after the storage has been suspended. */
 return 0;
fail:
 aster_power_session_finish(s,request,r);return r;
}

int aster_power_session_error(void *context){return ((aster_power_session_t *)context)->recovery_error;}

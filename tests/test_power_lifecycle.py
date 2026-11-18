"""Compile the shared mailbox, admission leases and teardown/rollback pipeline."""
import unittest
from test_waveshare import ROOT,run_c

class PowerLifecycle(unittest.TestCase):
 def test_mailbox_quiescence_and_recovery(self):
  run_c('#include "'+str(ROOT/'common/port/aster_power.c')+'"\n'+
        '#include "'+str(ROOT/'common/port/aster_power_session.c')+'"\n'+r"""
#include <assert.h>
static uint64_t now;
static bool app_busy,audio_busy,lease_finishes,network_off,storage_off;
static unsigned stopped,entered,stage4;
static int fail_stage,recovery_failure;
uint64_t aster_os_uptime_ms(void){return now;}
void aster_os_sleep_ms(uint32_t ms){now+=ms;if(lease_finishes&&aster_power_activity_count()){lease_finishes=false;aster_power_activity_end();}}
static int caps(void *ctx,aster_power_caps_t *out){(void)ctx;out->modes=ASTER_POWER_LIGHT_SLEEP|ASTER_POWER_RESTART;out->light_wake=ASTER_WAKE_KEY|ASTER_WAKE_TIMER;return 0;}
static int suspend_device(void *ctx,const aster_power_request_t *request){
 (void)ctx;(void)request;entered++;assert(network_off&&storage_off&&!aster_power_activity_count()&&aster_power_activity_closing());return 0;
}
static int restart(void *ctx){(void)ctx;return -EIO;}
static void stop_app(void *ctx){(void)ctx;stopped++;}
static bool busy_app(void *ctx){(void)ctx;return app_busy;}
static int audio(void *ctx,bool sleeping){(void)ctx;assert(aster_power_activity_closing());return sleeping&&audio_busy?-EBUSY:0;}
static int network(void *ctx,bool suspend){(void)ctx;if(suspend&&fail_stage==1)return -EIO;network_off=suspend;return 0;}
static int storage(void *ctx,bool suspend){(void)ctx;if(suspend&&fail_stage==2)return -EIO;if(!suspend&&recovery_failure)return -EIO;storage_off=suspend;return 0;}
static void ui_hook(void *ctx,const aster_power_request_t *request,unsigned stage,int result){
 (void)ctx;(void)request;(void)result;if(stage==4){stage4++;assert(!aster_power_activity_closing());}else assert(aster_power_activity_closing());
}
static const aster_power_session_ops_t hooks={audio,network,storage,ui_hook};
static aster_power_session_t session={.ops=&hooks};
int main(void){
 aster_power_ops_t board={.size=sizeof(board),.version=ASTER_POWER_API_VERSION,.name="test-board",.caps=caps,.suspend=suspend_device,.restart=restart};
 aster_power_lifecycle_t life={.size=sizeof(life),.version=ASTER_POWER_API_VERSION,.name="test-os",.prepare=aster_power_session_prepare,.finish=aster_power_session_finish,.recovery_error=aster_power_session_error};
 aster_power_dispatch_t dispatch={sizeof(dispatch),1,"owner",stop_app,busy_app};
 assert(!aster_power_bind(&board,NULL));assert(!aster_power_bind_lifecycle(&life,&session));assert(!aster_power_bind_dispatch(&dispatch,NULL));
 aster_power_request_t r={sizeof(r),ASTER_POWER_API_VERSION,ASTER_POWER_LIGHT_SLEEP,ASTER_WAKE_KEY,0};
 assert(!aster_power_activity_begin());assert(aster_power_activity_count()==1);
 lease_finishes=true;app_busy=true;assert(!aster_power_schedule(&r)&&stopped==1);
 assert(aster_power_schedule(&r)==-EBUSY);aster_power_poll();assert(!entered&&aster_power_pending());
 app_busy=false;aster_power_poll();assert(entered==1&&stage4==1&&!aster_power_pending());
 assert(!network_off&&!storage_off&&!aster_power_activity_closing());assert(aster_power_last_result()==0);
 audio_busy=true;assert(!aster_power_schedule(&r));aster_power_poll();assert(aster_power_last_result()==-EBUSY&&entered==1&&!aster_power_activity_closing());audio_busy=false;
 assert(!aster_power_activity_begin());assert(!aster_power_schedule(&r));aster_power_poll();assert(aster_power_last_result()==-EBUSY&&!network_off&&!storage_off&&!aster_power_activity_closing());aster_power_activity_end();
 fail_stage=2;assert(!aster_power_schedule(&r));aster_power_poll();assert(aster_power_last_result()==-EIO&&!network_off&&!storage_off);fail_stage=0;
 recovery_failure=1;assert(!aster_power_schedule(&r));aster_power_poll();assert(aster_power_last_result()==-EIO&&!aster_power_activity_closing());recovery_failure=0;storage_off=false;
 r.wake_sources=ASTER_WAKE_TIMER;assert(aster_power_schedule(&r)==-EINVAL);
 assert(!aster_power_activity_close());assert(aster_power_activity_begin()==-EBUSY);aster_power_activity_end();assert(aster_power_activity_closing()&&!aster_power_activity_count());aster_power_activity_reopen();
 aster_power_seal();assert(aster_power_bind_dispatch(&dispatch,NULL)==-EBUSY);
 return 0;
}
""")

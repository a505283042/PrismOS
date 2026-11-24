"""Actual shared state model: transfer is never reported as installation success."""
import unittest
from test_waveshare import ROOT,run_c

class SerialDeploy(unittest.TestCase):
    def test_progress_resume_error_success_and_stale_dismiss(self):
        source=ROOT/'common/port/aster_deploy.c'
        run_c('#include "'+str(source)+'"\n'+r'''
#include <assert.h>
static uint64_t now=100;
int aster_os_mutex_create(aster_os_mutex_t *m){m->handle=m;return 0;}
int aster_os_mutex_lock(aster_os_mutex_t *m,uint32_t timeout){(void)timeout;assert(m->handle);return 0;}
int aster_os_mutex_unlock(aster_os_mutex_t *m){assert(m->handle);return 0;}
uint64_t aster_os_uptime_ms(void){return now;}
int main(void){
 aster_deploy_snapshot s;assert(!aster_deploy_get(&s));assert(!aster_deploy_init());
 aster_deploy_begin("test.ast",1000,500);assert(aster_deploy_get(&s));assert(s.phase==ASTER_DEPLOY_RECEIVE&&s.done==500&&s.total==1000);
 unsigned first=s.generation;aster_deploy_dismiss(first);assert(aster_deploy_get(&s));
 aster_deploy_update(ASTER_DEPLOY_VERIFY,1000,1000,NULL);aster_deploy_finish(-5);
 assert(aster_deploy_get(&s)&&s.phase==ASTER_DEPLOY_ERROR&&s.failed_phase==ASTER_DEPLOY_VERIFY&&s.error==-5);
 now=200;aster_deploy_begin("test.ast",1000,500);aster_deploy_dismiss(first);
 assert(aster_deploy_get(&s)&&s.phase==ASTER_DEPLOY_RECEIVE&&s.since_ms==200);
 aster_deploy_update(ASTER_DEPLOY_RECEIVED,1000,1000,NULL);assert(aster_deploy_get(&s)&&s.phase!=ASTER_DEPLOY_DONE);
 aster_deploy_update(ASTER_DEPLOY_CHECK,0,0,"test.ast");aster_deploy_update(ASTER_DEPLOY_COPY,500,2000,"assets/image.bin");
 aster_deploy_finish(0);assert(aster_deploy_get(&s)&&s.phase==ASTER_DEPLOY_DONE&&s.done==2000&&!s.error);
 aster_deploy_dismiss(s.generation);assert(!aster_deploy_get(&s));return 0;
}
''')

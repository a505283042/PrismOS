/* SPDX-License-Identifier: MIT */
#include "aster_deploy.h"
#include "aster_os.h"
#include <stdio.h>
#include <string.h>
static aster_os_mutex_t lock;
static aster_deploy_snapshot status;
int aster_deploy_init(void){return lock.handle?0:aster_os_mutex_create(&lock);}
static bool enter(void){return lock.handle&&aster_os_mutex_lock(&lock,ASTER_OS_WAIT_FOREVER)==0;}
static void leave(void){(void)aster_os_mutex_unlock(&lock);}
void aster_deploy_begin(const char *file,uint64_t total,uint64_t offset){
 if(!enter())return;
 status.generation++;status.phase=ASTER_DEPLOY_RECEIVE;status.done=offset;status.total=total;status.error=0;status.since_ms=aster_os_uptime_ms();snprintf(status.file,sizeof(status.file),"%s",file?file:"");leave();
}
void aster_deploy_update(aster_deploy_phase phase,uint64_t done,uint64_t total,const char *file){
 if(!enter())return;
 if(status.phase==ASTER_DEPLOY_IDLE||status.phase==ASTER_DEPLOY_DONE||status.phase==ASTER_DEPLOY_ERROR){status.generation++;status.since_ms=aster_os_uptime_ms();}
 status.phase=phase;status.done=done;status.total=total;status.error=0;if(file)snprintf(status.file,sizeof(status.file),"%s",file);leave();
}
void aster_deploy_finish(int error){if(!enter())return;if(status.phase==ASTER_DEPLOY_IDLE){status.generation++;status.since_ms=aster_os_uptime_ms();}status.failed_phase=status.phase;status.phase=error?ASTER_DEPLOY_ERROR:ASTER_DEPLOY_DONE;status.error=error;if(!error)status.done=status.total;leave();}
bool aster_deploy_get(aster_deploy_snapshot *out){if(!out||!enter())return false;*out=status;leave();return out->phase!=ASTER_DEPLOY_IDLE;}
void aster_deploy_dismiss(uint32_t generation){if(!enter())return;if(status.generation==generation&&(status.phase==ASTER_DEPLOY_DONE||status.phase==ASTER_DEPLOY_ERROR||status.phase==ASTER_DEPLOY_RECEIVED))status.phase=ASTER_DEPLOY_IDLE;leave();}

/* SPDX-License-Identifier: MIT */
#include "aster_port_check.h"
#include "aster_device.h"
#include "aster_os.h"
#include "aster_services.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
static void task_entry(void *context){(void)context;}
int aster_port_check(aster_port_check_t *out){
 if(!out)return -EINVAL;
 *out=(aster_port_check_t){.stage="memory"};
 void *external=aster_os_alloc(128,ASTER_OS_MEM_EXTERNAL),*internal=aster_os_alloc(64,ASTER_OS_MEM_INTERNAL);
 if(!external||!internal){aster_os_free(external,ASTER_OS_MEM_EXTERNAL);aster_os_free(internal,ASTER_OS_MEM_INTERNAL);out->error=-ENOMEM;return out->error;}
 memset(external,0xa5,128);memset(internal,0x5a,64);aster_os_free(external,ASTER_OS_MEM_EXTERNAL);aster_os_free(internal,ASTER_OS_MEM_INTERNAL);out->passed|=1;
 out->stage="clock";uint64_t before=aster_os_uptime_ms();aster_os_sleep_ms(2);if(aster_os_uptime_ms()<before){out->error=-EIO;return out->error;}
 aster_time_info_t time;if((out->error=aster_os_wall_clock(&time)))return out->error;out->passed|=2;
 out->stage="mutex";aster_os_mutex_t mutex={0};int r=aster_os_mutex_create(&mutex);if(r){out->error=r;return r;}
 r=aster_os_mutex_lock(&mutex,100);if(!r){r=aster_os_mutex_lock(&mutex,0);if(!r)r=aster_os_mutex_unlock(&mutex);int unlock=aster_os_mutex_unlock(&mutex);if(!r)r=unlock;}
 int cleanup=aster_os_mutex_destroy(&mutex);if(!r)r=cleanup;if(r){out->error=r;return r;}out->passed|=4;
 out->stage="semaphore";aster_os_sem_t sem={0};r=aster_os_sem_create(&sem,0,1);if(r){out->error=r;return r;}
 r=aster_os_sem_take(&sem,0);
 if(r==-EAGAIN || r==-EBUSY || r==-ETIMEDOUT){r=aster_os_sem_give(&sem);if(!r)r=aster_os_sem_take(&sem,100);}
 else if(r>=0)r=-EIO;
 cleanup=aster_os_sem_destroy(&sem);if(!r)r=cleanup;if(r){out->error=r;return r;}out->passed|=8;
 out->stage="task";aster_os_task_t task={0};aster_os_task_config_t config={.name="port-check",.stack_bytes=4096,.priority=8,.core=-1};
 r=aster_os_task_start(&task,&config,task_entry,NULL);if(!r)r=aster_os_task_join(&task,1000);if(r){out->error=r;return r;}out->passed|=16;
 out->stage="system";aster_system_info_t profile={0};r=aster_service_system_info(&profile);if(!r&&(!profile.board[0]||!profile.abi_version))r=-EIO;if(r){out->error=r;return r;}out->passed|=32;
 out->stage="display";aster_display_info_t info={0};r=aster_device_display_get_info(0,&info);if(!r&&(!info.width||!info.height))r=-EIO;if(r){out->error=r;return r;}out->passed|=64;
 out->stage="filesystem";if(!aster_service_storage_mounted()){out->error=-ENODEV;return out->error;}
 char path[96],renamed[100];aster_os_entry_t entry;unsigned attempt;
 for(attempt=0;attempt<16;attempt++){snprintf(path,sizeof(path),"/SD:/aster/system/port-check-%08x-%u.tmp",(unsigned)aster_os_cycle_us(),attempt);r=aster_os_stat(path,&entry);if(r==-ENOENT)break;if(r) {out->error=r;return r;}}
 if(attempt==16){out->error=-EEXIST;return out->error;}snprintf(renamed,sizeof(renamed),"%s.new",path);
 r=aster_os_stat(renamed,&entry);if(r!=-ENOENT){out->error=r?r:-EEXIST;return out->error;}
 aster_os_file_t file={0};aster_os_dir_t dir={0};bool created=false,moved=false;
 r=aster_os_file_open(&file,path,ASTER_OS_READ|ASTER_OS_WRITE|ASTER_OS_CREATE|ASTER_OS_TRUNCATE);
 if(!r){created=true;const char data[]="ASTER-port";char buffer[sizeof(data)]={0};
  int n=aster_os_file_write(&file,data,sizeof(data));if(n!=(int)sizeof(data))r=n<0?n:-EIO;
  if(!r)r=aster_os_file_sync(&file);
  if(!r)r=aster_os_file_seek(&file,0,ASTER_OS_SEEK_SET);
  if(!r){n=aster_os_file_read(&file,buffer,sizeof(buffer));if(n!=(int)sizeof(data)||memcmp(buffer,data,sizeof(data)))r=-EIO;}
  cleanup=aster_os_file_close(&file);if(!r)r=cleanup;
  if(!r)r=aster_os_stat(path,&entry);
  if(!r&&entry.size!=sizeof(data))r=-EIO;
 }
 if(!r){r=aster_os_rename(path,renamed);if(!r)moved=true;}
 if(!r){r=aster_os_dir_open(&dir,"/SD:/aster/system");if(!r){r=aster_os_dir_read(&dir,&entry);cleanup=aster_os_dir_close(&dir);if(!r)r=cleanup;}}
 if(created){cleanup=aster_os_unlink(moved?renamed:path);if(!r)r=cleanup;}
 if(!r){aster_os_volume_t volume;r=aster_os_volume("/SD:",&volume);if(!r&&!volume.block_size)r=-EIO;}
 if(r){out->error=r;return r;}out->passed|=128;out->stage="done";return 0;
}

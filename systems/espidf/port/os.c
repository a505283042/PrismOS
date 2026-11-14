/* SPDX-License-Identifier: MIT */
#include "aster_os.h"
#include "aster_mem.h"
#include "aster_time.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <errno.h>
#include "../../shared/port/files.inc"
static void *alloc(size_t n,unsigned region){return region==ASTER_OS_MEM_EXTERNAL?aster_psram_alloc(n):region==ASTER_OS_MEM_INTERNAL?aster_dram_alloc(n):NULL;}
static void release(void *p,unsigned region){if(region==ASTER_OS_MEM_EXTERNAL)aster_psram_free(p);else if(region==ASTER_OS_MEM_INTERNAL)aster_dram_free(p);}
static uint64_t uptime(void){return (uint64_t)esp_timer_get_time()/1000;}
static uint32_t cycles(void){return (uint32_t)esp_timer_get_time();}
static TickType_t timeout(uint32_t ms){TickType_t ticks=pdMS_TO_TICKS(ms);return ms==ASTER_OS_WAIT_FOREVER?portMAX_DELAY:ms?(ticks?ticks:1):0;}
static void sleep_ms(uint32_t ms){if(ms)vTaskDelay(timeout(ms));else taskYIELD();}
static int mutex_create(aster_os_mutex_t *m){if(!m||m->handle)return -EINVAL;m->handle=xSemaphoreCreateRecursiveMutex();return m->handle?0:-ENOMEM;}
static int mutex_lock(aster_os_mutex_t *m,uint32_t ms){if(!m||!m->handle)return -EINVAL;return xSemaphoreTakeRecursive(m->handle,timeout(ms))==pdTRUE?0:-EAGAIN;}
static int mutex_unlock(aster_os_mutex_t *m){if(!m||!m->handle)return -EINVAL;return xSemaphoreGiveRecursive(m->handle)==pdTRUE?0:-EPERM;}
static int mutex_destroy(aster_os_mutex_t *m){if(!m||!m->handle)return -EINVAL;vSemaphoreDelete(m->handle);m->handle=NULL;return 0;}
static int sem_create(aster_os_sem_t *s,unsigned initial,unsigned limit){if(!s||s->handle||!limit||initial>limit)return -EINVAL;s->handle=xSemaphoreCreateCounting(limit,initial);return s->handle?0:-ENOMEM;}
static int sem_take(aster_os_sem_t *s,uint32_t ms){if(!s||!s->handle)return -EINVAL;return xSemaphoreTake(s->handle,timeout(ms))==pdTRUE?0:-EAGAIN;}
static int sem_give(aster_os_sem_t *s){if(!s||!s->handle)return -EINVAL;/* Both backends saturate at the configured count. */(void)xSemaphoreGive(s->handle);return 0;}
static int sem_destroy(aster_os_sem_t *s){if(!s||!s->handle)return -EINVAL;vSemaphoreDelete(s->handle);s->handle=NULL;return 0;}
struct task_record {SemaphoreHandle_t done;aster_os_task_fn entry;void *context;};
static void task_run(void *arg){struct task_record *r=arg;r->entry(r->context);xSemaphoreGive(r->done);vTaskDelete(NULL);}
static int task_start(aster_os_task_t *out,const aster_os_task_config_t *config,aster_os_task_fn entry,void *context){
 if(!out||out->handle||!config||!entry||config->stack_bytes<1024||config->stack_bytes>65536||config->priority>=configMAX_PRIORITIES||config->core< -1||config->core>=portNUM_PROCESSORS)return -EINVAL;
 struct task_record *r=calloc(1,sizeof(*r));if(!r)return -ENOMEM;r->done=xSemaphoreCreateBinary();if(!r->done){free(r);return -ENOMEM;}
 r->entry=entry;r->context=context;TaskHandle_t task;
 BaseType_t result=xTaskCreatePinnedToCore(task_run,config->name?config->name:"aster-port",config->stack_bytes,r,configMAX_PRIORITIES-1-config->priority,&task,config->core<0?tskNO_AFFINITY:config->core);
 if(result!=pdPASS){vSemaphoreDelete(r->done);free(r);return -ENOMEM;}out->handle=r;return 0;
}
static int task_join(aster_os_task_t *out,uint32_t ms){if(!out||!out->handle)return -EINVAL;struct task_record *r=out->handle;if(xSemaphoreTake(r->done,timeout(ms))!=pdTRUE)return -EAGAIN;vSemaphoreDelete(r->done);free(r);out->handle=NULL;return 0;}
const aster_os_ops_t aster_os_backend={
 .size=sizeof(aster_os_ops_t),.version=ASTER_OS_API_VERSION,.name="espidf",
 .alloc=alloc,.free=release,.uptime_ms=uptime,.cycle_us=cycles,.sleep_ms=sleep_ms,.wall_clock=aster_time_read,
 ASTER_OS_FILE_BINDINGS,
 .mutex_create=mutex_create,.mutex_lock=mutex_lock,.mutex_unlock=mutex_unlock,.mutex_destroy=mutex_destroy,
 .sem_create=sem_create,.sem_take=sem_take,.sem_give=sem_give,.sem_destroy=sem_destroy,
 .task_start=task_start,.task_join=task_join
};

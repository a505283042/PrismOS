/* SPDX-License-Identifier: MIT */
#include "aster_os.h"
#include "aster_mem.h"
#include "aster_time.h"
#include <zephyr/kernel.h>
#include <errno.h>
#include "../../shared/port/files.inc"
static void *alloc(size_t n,unsigned region){return region==ASTER_OS_MEM_EXTERNAL?aster_psram_alloc(n):region==ASTER_OS_MEM_INTERNAL?aster_dram_alloc(n):NULL;}
static void release(void *p,unsigned region){if(region==ASTER_OS_MEM_EXTERNAL)aster_psram_free(p);else if(region==ASTER_OS_MEM_INTERNAL)aster_dram_free(p);}
static uint64_t uptime(void){return (uint64_t)k_uptime_get();}
static uint32_t cycles(void){return (uint32_t)k_cyc_to_us_floor64(k_cycle_get_32());}
static void sleep_ms(uint32_t ms){k_msleep(ms);}
static k_timeout_t timeout(uint32_t ms){return ms==ASTER_OS_WAIT_FOREVER?K_FOREVER:K_MSEC(ms);}
static int mutex_create(aster_os_mutex_t *m){if(!m||m->handle)return -EINVAL;struct k_mutex *p=k_malloc(sizeof(*p));if(!p)return -ENOMEM;k_mutex_init(p);m->handle=p;return 0;}
static int mutex_lock(aster_os_mutex_t *m,uint32_t ms){return m&&m->handle?k_mutex_lock(m->handle,timeout(ms)):-EINVAL;}
static int mutex_unlock(aster_os_mutex_t *m){return m&&m->handle?k_mutex_unlock(m->handle):-EINVAL;}
static int mutex_destroy(aster_os_mutex_t *m){if(!m||!m->handle)return -EINVAL;k_free(m->handle);m->handle=NULL;return 0;}
static int sem_create(aster_os_sem_t *s,unsigned initial,unsigned limit){if(!s||s->handle||!limit||initial>limit)return -EINVAL;struct k_sem *p=k_malloc(sizeof(*p));if(!p)return -ENOMEM;int r=k_sem_init(p,initial,limit);if(r){k_free(p);return r;}s->handle=p;return 0;}
static int sem_take(aster_os_sem_t *s,uint32_t ms){return s&&s->handle?k_sem_take(s->handle,timeout(ms)):-EINVAL;}
static int sem_give(aster_os_sem_t *s){if(!s||!s->handle)return -EINVAL;k_sem_give(s->handle);return 0;}
static int sem_destroy(aster_os_sem_t *s){if(!s||!s->handle)return -EINVAL;k_free(s->handle);s->handle=NULL;return 0;}
struct task_record {struct k_thread thread;k_thread_stack_t *stack;aster_os_task_fn entry;void *context;};
static void task_run(void *a,void *b,void *c){(void)b;(void)c;struct task_record *r=a;r->entry(r->context);}
static int task_start(aster_os_task_t *out,const aster_os_task_config_t *config,aster_os_task_fn entry,void *context){
 if(!out||out->handle||!config||!entry||config->stack_bytes<1024||config->stack_bytes>65536||config->priority>=CONFIG_NUM_PREEMPT_PRIORITIES||config->core< -1||config->core>=CONFIG_MP_MAX_NUM_CPUS)return -EINVAL;
 if(config->core>=0 && !IS_ENABLED(CONFIG_SCHED_CPU_MASK))return -ENOTSUP;
 struct task_record *r=k_calloc(1,sizeof(*r));if(!r)return -ENOMEM;
 r->stack=k_thread_stack_alloc(config->stack_bytes,0);if(!r->stack){k_free(r);return -ENOMEM;}
 r->entry=entry;r->context=context;k_thread_create(&r->thread,r->stack,config->stack_bytes,task_run,r,NULL,NULL,K_PRIO_PREEMPT(config->priority),0,K_FOREVER);
#if defined(CONFIG_SCHED_CPU_MASK)
 if(config->core>=0){int error=k_thread_cpu_pin(&r->thread,config->core);if(error){k_thread_abort(&r->thread);k_thread_stack_free(r->stack);k_free(r);return error;}}
#endif
 if(config->name)(void)k_thread_name_set(&r->thread,config->name);
 out->handle=r;k_thread_start(&r->thread);return 0;
}
static int task_join(aster_os_task_t *out,uint32_t ms){if(!out||!out->handle)return -EINVAL;struct task_record *r=out->handle;int result=k_thread_join(&r->thread,timeout(ms));if(result)return result;result=k_thread_stack_free(r->stack);if(result)return result;k_free(r);out->handle=NULL;return 0;}
const aster_os_ops_t aster_os_backend={
 .size=sizeof(aster_os_ops_t),.version=ASTER_OS_API_VERSION,.name="zephyr",
 .alloc=alloc,.free=release,.uptime_ms=uptime,.cycle_us=cycles,.sleep_ms=sleep_ms,.wall_clock=aster_time_read,
 ASTER_OS_FILE_BINDINGS,
 .mutex_create=mutex_create,.mutex_lock=mutex_lock,.mutex_unlock=mutex_unlock,.mutex_destroy=mutex_destroy,
 .sem_create=sem_create,.sem_take=sem_take,.sem_give=sem_give,.sem_destroy=sem_destroy,
 .task_start=task_start,.task_join=task_join
};

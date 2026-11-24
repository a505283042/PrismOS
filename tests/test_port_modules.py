"""Compile all public ports without either embedded OS and exercise dispatch."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class Ports(unittest.TestCase):
    def test_os_services_network_without_embedded_headers(self):
        cc = shutil.which('cc') or shutil.which('gcc')
        if not cc:
            self.skipTest('C compiler required')
        with tempfile.TemporaryDirectory() as folder:
            fixture = Path(folder) / 'ports.c'
            fixture.write_text(
                r'''
#include "aster_os.h"
#include "aster_services.h"
#include "aster_network.h"
#include <aster/aster.h>
#include "aster_port_util.h"
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
static unsigned calls;
static void *allocate(size_t n,unsigned region){assert(region==ASTER_OS_MEM_EXTERNAL);calls++;return malloc(n);}
static void release(void *p,unsigned region){assert(region==ASTER_OS_MEM_EXTERNAL);calls++;free(p);}
static uint64_t uptime(void){return UINT64_C(0x100000001);}
static int read_original(aster_os_file_t *f,void *data,size_t n){assert(f&&data&&n==4);memcpy(data,"test",4);return 4;}
static int count(void){return 4;}
static int child_count(void){return 7;}
static int http_start(uint64_t owner,const struct aster_http_options *options){assert(owner==123&&options->timeout_ms==30000&&!strcmp(options->url,"https://example.org"));return 9;}
static int busy_status(uint64_t owner,uint32_t handle,struct aster_http_snapshot *out){assert(owner==123&&handle==9&&out);return -EAGAIN;}
static void logged(unsigned level,const char *message){assert(level==3&&!strcmp(message,"port 42"));calls++;}
const aster_os_ops_t aster_os_backend={.size=sizeof(aster_os_ops_t),.version=ASTER_OS_API_VERSION,.name="host",.alloc=allocate,.free=release,.uptime_ms=uptime,.file_read=read_original};
const aster_services_ops_t aster_services_backend={.size=sizeof(aster_services_ops_t),.version=ASTER_SERVICES_API_VERSION,.name="host",.app_count=count,.log=logged};
const aster_network_ops_t aster_network_backend={.size=sizeof(aster_network_ops_t),.version=ASTER_NETWORK_API_VERSION,.name="host",.http_start=http_start,.http_status=busy_status};
int main(void){
 _Static_assert(ASTER_NETWORK_IO_MAX>0,"guest network contract must coexist with the port header");
 _Static_assert(IS_ENABLED(1)==1,"enabled");_Static_assert(IS_ENABLED(UNDEFINED_OPTION)==0,"undefined");
 void *p=aster_os_alloc_external(16);assert(p);aster_os_free_external(p);assert(calls==2);
 assert(aster_os_uptime32()==1);assert(aster_os_cycle_us()==0);
 aster_os_file_t f={0};char data[4];assert(aster_os_file_read(&f,data,4)==4&&!memcmp(data,"test",4));
 assert(aster_os_file_open(&f,"/missing",ASTER_OS_READ)==-ENOTSUP);
 assert(aster_os_task_start(NULL,NULL,NULL,NULL)==-ENOTSUP);
 aster_os_ops_t os,override={.size=sizeof(os),.version=ASTER_OS_API_VERSION,.name="child"};
 assert(!aster_os_derive(&os,&aster_os_backend,&override));assert(!aster_os_bind(&os));assert(aster_os_uptime32()==1);
 override.size=0;assert(aster_os_bind(&override)==-EINVAL);assert(!strcmp(aster_os_ops()->name,"child"));
 assert(aster_service_app_count()==4);aster_service_logf(3,"port %d",42);assert(calls==3);
 aster_services_ops_t service,service_override={.size=sizeof(service),.version=ASTER_SERVICES_API_VERSION,.name="child",.app_count=child_count};
 assert(!aster_services_derive(&service,&aster_services_backend,&service_override));assert(!aster_services_bind(&service));
 assert(aster_service_app_count()==7);assert(service.log==logged);assert(!aster_service_app_find("none",NULL));
 struct aster_http_options options={.url="https://example.org",.timeout_ms=30000};
 assert(aster_network_http_start(123,&options)==9);struct aster_http_snapshot status={0};assert(aster_network_http_status(123,9,&status)==-EAGAIN);
 assert(aster_network_ws_open(1,"wss://example.org",1000)==-ENOTSUP);
 aster_network_ops_t network,network_override={.size=sizeof(network),.version=ASTER_NETWORK_API_VERSION,.name="child"};
 assert(!aster_network_derive(&network,&aster_network_backend,&network_override));assert(!aster_network_bind(&network));assert(aster_network_http_start(123,&options)==9);
 aster_os_seal();aster_services_seal();aster_network_seal();
 assert(aster_os_bind(&aster_os_backend)==-EBUSY);assert(aster_services_bind(&aster_services_backend)==-EBUSY);assert(aster_network_bind(&aster_network_backend)==-EBUSY);
 return 0;
}'''
            )
            binary = Path(folder) / 'ports'
            subprocess.run(
                [
                    cc,
                    '-std=c11',
                    '-Wall',
                    '-Wextra',
                    '-Werror',
                    '-pedantic',
                    '-I' + str(ROOT / 'common/port'),
                    '-I' + str(ROOT / 'common/net'),
                    '-I' + str(ROOT / 'contract/include'),
                    str(fixture),
                    *[
                        str(ROOT / 'common/port' / name)
                        for name in ['aster_os.c', 'aster_services.c', 'aster_network.c']
                    ],
                    '-o',
                    str(binary),
                ],
                check=True,
            )
            subprocess.run([str(binary)], check=True)

    def test_ui_port_boundary(self):
        ui = ROOT / 'systems/zephyr/ui'
        for name in [
            'launcher/ui_shell.c',
            'launcher/ui_shell_lifecycle.c',
            'launcher/ui_shell_input.c',
            'launcher/flux.c',
            'font_sd.c',
            'ui_font.c',
            'qpanel.c',
            'wifi_settings.c',
            'ui_splash.c',
            'flux_draw.c',
            'ui_wifi.h',
            'ui_geom.h',
        ]:
            source = (ui / name).read_text()
            for forbidden in [
                '#include <zephyr/',
                '#include "aster_app_mgr.h"',
                '#include "aster_storage.h"',
                '#include "aster_runtime.h"',
                '#include "aster_kv.h"',
            ]:
                self.assertNotIn(forbidden, source, name)


if __name__ == '__main__':
    unittest.main()

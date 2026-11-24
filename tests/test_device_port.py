"""Compile the real dispatch layer without Zephyr or ESP-IDF headers."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class DevicePortTest(unittest.TestCase):
    def test_inventory_covers_platform(self):
        source = re.sub(r'/\*.*?\*/', '', (ROOT/'common/port/aster_platform.h').read_text(), flags=re.S)
        expected = set(re.findall(r'\baster_plat_(\w+)\s*\([^;{}]*\)\s*;', source))
        inventory = (ROOT/'common/port/aster_device_ops.def').read_text()
        actual = set(re.findall(r'^ASTER_DEVICE_OP\([^,]+, (\w+),', inventory, re.M))
        self.assertEqual(expected, actual)

    def test_dispatch_inheritance_and_sealing(self):
        cc = shutil.which('cc') or shutil.which('gcc')
        if not cc:
            self.skipTest('C compiler required')
        with tempfile.TemporaryDirectory() as folder:
            fixture = Path(folder)/'port.c'
            fixture.write_text(r'''
#include "aster_device.h"
#include <assert.h>
#include <errno.h>
#include <string.h>
static int volume=12;
static int get(void){return volume;}
static int set(int value){volume=value;return 0;}
static int overridden(void){return 99;}
static int write_at(const uint8_t *p,uint32_t x,uint32_t y,uint32_t w,uint32_t h,uint32_t pitch)
{assert(p && x==3 && y==4 && w==5 && h==6 && pitch==8);return 42;}
const aster_device_ops_t aster_device_backend={
 .size=sizeof(aster_device_ops_t),.version=ASTER_DEVICE_API_VERSION,.name="test",
 .audio_get_volume=get,.audio_set_volume=set,.display_write_at=write_at};
int main(void){
 assert(aster_device_audio_get_volume()==12);
 assert(!aster_device_audio_set_volume(33));assert(aster_device_audio_get_volume()==33);
 uint8_t p=0;assert(aster_device_display_write_at(&p,3,4,5,6,8)==42);
 assert(aster_device_cam_info(NULL,NULL)==ASTER_ENOTSUP);
 assert(!aster_device_display_canvas_ready());assert(!aster_device_display_canvas_back());
 assert(!aster_device_display_generation());aster_device_audio_ui_stop();
 assert(!aster_device_has(ASTER_DEVICE_METHOD_cam_info));assert(!aster_device_has(-1));
 assert(aster_device_has(ASTER_DEVICE_METHOD_audio_get_volume));
 aster_device_ops_t derived,override={.size=sizeof(override),.version=ASTER_DEVICE_API_VERSION,.name="child",.audio_get_volume=overridden};
 assert(!aster_device_derive(&derived,&aster_device_backend,&override));
 assert(!aster_device_bind(&derived));assert(!strcmp(aster_device_ops()->name,"child"));
 assert(aster_device_audio_get_volume()==99);assert(!aster_device_audio_set_volume(44));assert(volume==44);
 override.version=999;assert(aster_device_bind(&override)==-EINVAL);
 assert(aster_device_derive(&derived,&aster_device_backend,&override)==-EINVAL);
 assert(aster_device_audio_get_volume()==99);
 override.version=ASTER_DEVICE_API_VERSION;
 assert(!aster_device_derive(&override,&aster_device_backend,&override));
 assert(override.audio_set_volume==set && override.audio_get_volume==overridden);
 aster_device_seal();assert(aster_device_bind(&aster_device_backend)==-EBUSY);
 return 0;
}''')
            binary=Path(folder)/'port-test'
            subprocess.run([cc,'-std=c11','-Wall','-Wextra','-Werror','-pedantic',
                '-I'+str(ROOT/'common/port'),'-I'+str(ROOT/'platform'),
                '-I'+str(ROOT/'contract/include'),str(fixture),
                str(ROOT/'common/port/aster_device.c'),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True)

if __name__ == '__main__':
    unittest.main()

"""Exercise real thumbnail assembly and publication with a delayed SD writer."""
import unittest
import subprocess
import tempfile
from pathlib import Path

from test_waveshare import ROOT, run_c


class BluetoothCover(unittest.TestCase):
    def test_image_decoder_coexists_with_rom_symbols(self):
        vendor = ROOT / "modules/imgdec/vendor"
        with tempfile.TemporaryDirectory() as tmp:
            work = Path(tmp)
            (work / "rom.c").write_text(
                "void jd_prepare(void) {}\nvoid jd_decomp(void) {}\n"
                "void jd_restart(void) {}\nvoid jd_mcu_load(void) {}\nvoid jd_mcu_output(void) {}\n"
            )
            (work / "caller.c").write_text(r'''
#include "tjpgd.h"
#include <assert.h>
static size_t empty(JDEC *d, uint8_t *out, size_t size) {
    (void)d; (void)out; (void)size; return 0;
}
int main(void) {
    JDEC decoder;
    uint32_t pool[3072];
    assert(jd_prepare(&decoder, empty, pool, sizeof(pool), NULL) == JDR_INP);
}
''')
            exe = work / "link-check"
            subprocess.run(["cc", "-std=c11", "-I", str(vendor), str(work / "caller.c"),
                            str(vendor / "tjpgd.c"), str(work / "rom.c"), "-o", str(exe)],
                           check=True)
            subprocess.run([str(exe)], check=True)

    def test_latest_complete_image_only(self):
        source = (ROOT / "systems/espidf/components/aster_audio/bt_cover.c").read_text()
        state = source[source.index("#define COVER_DIR"):source.index("/* Called serially")]
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#define ESP_AVRC_CA_IMAGE_HANDLE_LEN 7
#define ESP_OK 0
#define ESP_BT_STATUS_SUCCESS 0
#define portMUX_TYPE int
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) ((void)p)
#define portEXIT_CRITICAL(p) ((void)p)
#define ESP_LOGI(...) ((void)0)
#define pdPASS 1
#define heap_caps_malloc(n, flags) malloc(n)
#define vTaskDelete(task) ((void)0)
enum { ASTER_OS_CREATE=1, ASTER_OS_WRITE=2, ASTER_OS_TRUNCATE=4 };
typedef struct { int open; } aster_os_file_t;
typedef struct { struct { int status; unsigned data_len; uint8_t *p_data; bool final; }
    cover_art_data; } esp_avrc_ct_cb_param_t;
static unsigned requests, writes;
static void (*worker)(void *);
static int xTaskCreate(void (*entry)(void *), const char *name, int stack, void *p, int prio, void *h) {
    (void)name; (void)stack; (void)p; (void)prio; (void)h; worker = entry; return 1;
}
static int esp_avrc_ct_cover_art_get_linked_thumbnail(uint8_t *handle) {
    assert(handle[0]=='0'); requests++; return 0;
}
static int aster_os_mkdir(const char *path) { (void)path; return -EEXIST; }
static int aster_os_file_open(aster_os_file_t *f, const char *path, int flags) {
    assert(strstr(path,"/SD:/music/.aster-bluetooth/cover")); (void)flags; f->open=1; return 0;
}
static int aster_os_file_close(aster_os_file_t *f) { assert(f->open); return 0; }
static int aster_os_file_write(aster_os_file_t *f, const void *bytes, size_t size) {
    assert(f->open && !memcmp(bytes,"\xff\xd8\xff\xd9",4)); writes++; return (int)size;
}
''' + state + r'''
int main(void) {
    uint8_t a[]="0000001", b[]="0000002", jpg[]={255,216,255,217};
    esp_avrc_ct_cb_param_t frame = {.cover_art_data={0,2,jpg,false}};
    linked=true;
    set_handle(a,7); assert(requests==1);
    data_event(&frame);
    set_handle(b,7); assert(requests==1);
    frame.cover_art_data.final=true; frame.cover_art_data.p_data=jpg+2;
    data_event(&frame); assert(!writer && requests==2);
    frame.cover_art_data.p_data=jpg; frame.cover_art_data.data_len=4;
    data_event(&frame); assert(writer && !saved);
    worker(NULL); assert(saved==epoch && writes==1 && revision==1);
    set_handle(b,7); assert(requests==2);
    set_handle(a,7); data_event(&frame); assert(writer && !saved);
    aster_bt_cover_reset(); worker(NULL); assert(!saved && revision==1);
    linked=true; set_handle(a,7);
    frame.cover_art_data.data_len=COVER_LIMIT+1;
    data_event(&frame); assert(!writer && !incoming && !saved);
    aster_bt_cover_reset();
}
''')

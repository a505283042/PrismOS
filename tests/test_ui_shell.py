"""用原 UI Shell 和独立桩验证主题注册、调度、安全切换及回退。"""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
UI = ROOT / "systems/zephyr/ui/launcher"

STUBS = {
    "aster_device.h": """#include <stdbool.h>
enum { ASTER_DEVICE_METHOD_display_flush_drain_checked };
bool aster_device_has(int);
int aster_device_display_flush_drain_checked(void);
void aster_device_display_flush_drain(void);
""",
    "ui_lvgl.h": """void aster_ui_lvgl_request_frame(void);
void aster_ui_lvgl_pump(void);
""",
    "aster_power.h": """#include <stdbool.h>
bool aster_power_pending(void);
bool aster_power_activity_closing(void);
""",
    "aster_services.h": """#include <stddef.h>
int aster_service_kv_get(const char *,const char *,void *,size_t,size_t *);
int aster_service_kv_set(const char *,const char *,const void *,size_t);
void aster_service_logf(unsigned,const char *,...);
""",
    "aster_port_util.h": "#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))\n",
    "ui_shell_lifecycle.h": "#include <stdbool.h>\nbool aster_ui_shell_boot_busy(void);\n",
    "ui_shell_input.h": """#include <stdbool.h>
bool aster_ui_shell_input_contacts_down(void);
bool aster_ui_shell_power_is_off(void);
""",
    "ui_shell_settings.h": """#include "qpanel.h"
void aster_ui_shell_settings_bind_panel(const aster_qpanel_theme_ops_t *);
bool aster_ui_shell_settings_pages_open(void);
void aster_ui_shell_settings_request_exit(void);
""",
    "ui_shell_state.h": """#include <stdbool.h>
bool aster_ui_shell_storage_mounted(void);
""",
}

FIXTURE = r"""
#include <assert.h>
#include <string.h>
#include <stdbool.h>
#include <stddef.h>
#include "ui_shell.h"
#include "ui_theme.h"

static bool fvis,pvis,fail,contacts,off,pages,power,closing,exit_open,boot_busy;
static bool drain_fail,frame_fail,kv_fail;
static unsigned frames,writes,drains,exits;
static int page,steps;
static char stored[32];
static const aster_qpanel_theme_ops_t *panel;

static int init(void) {return 0;}
static bool fshow(void) {assert(!pvis);fvis=true;return true;}
static bool pshow(void) {assert(!fvis);pvis=true;return !fail;}
static void fhide(aster_ui_hide_reason_t reason) {assert(reason==ASTER_UI_HIDE_APP || reason==ASTER_UI_HIDE_SWITCH);fvis=false;}
static void phide(aster_ui_hide_reason_t reason) {assert(reason==ASTER_UI_HIDE_SWITCH || reason==ASTER_UI_HIDE_APP);pvis=false;}
static bool fv(void) {return fvis;}
static bool pv(void) {return pvis;}
static void request(void) {}
static void setpage(int p) {page=p;}
static void step(int d) {steps+=d;}
static void tick(void) {}
static uint32_t wait(void) {return 37;}
static void palette(aster_qpanel_palette_t *p) {p->bg=1;}
static void art(uint32_t *a,uint32_t *b,uint32_t *c) {*a=1;*b=2;*c=3;}
const aster_ui_theme_t aster_ui_theme_flux = {
 .id="flux",.name="FLUX",.init=init,.show=fshow,.hide=fhide,
 .request_show=request,.show_page=setpage,.step_page=step,.tick=tick,
 .idle_wait_ms=wait,.is_visible=fv,.can_switch=fv,.palette=palette,.art_stats=art};
const aster_ui_theme_t aster_ui_theme_probe = {
 .id="probe",.name="Probe",.init=init,.show=pshow,.hide=phide,
 .request_show=request,.show_page=setpage,.step_page=step,.tick=tick,
 .idle_wait_ms=wait,.is_visible=pv,.can_switch=pv,.palette=palette};

bool aster_device_has(int method) {(void)method;return true;}
int aster_device_display_flush_drain_checked(void) {drains++;return drain_fail?-1:0;}
void aster_device_display_flush_drain(void) {assert(0);}
void aster_ui_lvgl_request_frame(void) {}
void aster_ui_lvgl_pump(void) {frames++;if(frame_fail) aster_ui_shell_theme_report_display_error();}
bool aster_power_pending(void) {return power;}
bool aster_power_activity_closing(void) {return closing;}
bool aster_ui_shell_input_contacts_down(void) {return contacts;}
bool aster_ui_shell_boot_busy(void) {return boot_busy;}
bool aster_ui_shell_power_is_off(void) {return off;}
bool aster_ui_shell_settings_pages_open(void) {return pages;}
bool aster_qpanel_exit_is_open(void) {return exit_open;}
bool aster_ui_shell_storage_mounted(void) {return true;}
void aster_ui_shell_settings_bind_panel(const aster_qpanel_theme_ops_t *ops) {panel=ops;}
void aster_ui_shell_settings_request_exit(void) {exits++;}
void aster_launcher_slider_stats(aster_launcher_slider_stats_t *out) {out->value=42;}
void aster_service_logf(unsigned level,const char *s,...) {(void)level;(void)s;}
int aster_service_kv_get(const char *owner,const char *key,void *data,size_t cap,size_t *bytes)
{
 assert(!strcmp(owner,"prism.ui") && !strcmp(key,"theme"));
 if(!stored[0]) return -1;
 *bytes=strlen(stored)+1;assert(*bytes<=cap);memcpy(data,stored,*bytes);return 0;
}
int aster_service_kv_set(const char *owner,const char *key,const void *data,size_t bytes)
{
 assert(!strcmp(owner,"prism.ui") && !strcmp(key,"theme"));
 assert(frames>0 && bytes<=sizeof(stored));writes++;
 if(kv_fail)return -1;
 memcpy(stored,data,bytes);return 0;
}

int main(int argc,char **argv)
{
 const char *scenario=argc>1?argv[1]:"switch";
 if(!strcmp(scenario,"restore")) strcpy(stored,"probe");
 if(!strcmp(scenario,"invalid")) strcpy(stored,"missing");
 assert(!aster_ui_shell_init());
 aster_ui_shell_show();
 aster_ui_shell_bind_panel();
 assert(fvis && !pvis && panel->count()==2);
 assert(aster_ui_shell_idle_wait_ms()==37);
 aster_ui_shell_pump_idle();
 if(!strcmp(scenario,"restore")) {assert(pvis && !fvis);return 0;}
 if(!strcmp(scenario,"invalid")) {assert(fvis && !pvis && !writes);return 0;}
 if(!strcmp(scenario,"drain")) {
  drain_fail=true;assert(aster_ui_shell_theme_request_id("probe"));
  aster_ui_shell_pump_idle();assert(fvis && !pvis && !writes);
  drain_fail=false;aster_ui_shell_pump_idle();assert(fvis && !pvis);return 0;
 }
 if(!strcmp(scenario,"frame")) {
  frame_fail=true;assert(aster_ui_shell_theme_request_id("probe"));
  aster_ui_shell_pump_idle();assert(pvis && !fvis && !writes);
  assert(aster_ui_shell_theme_request_id("flux"));
  aster_ui_shell_pump_idle();assert(pvis && !fvis);return 0;
 }
 if(!strcmp(scenario,"kv")) {
  kv_fail=true;assert(aster_ui_shell_theme_request_id("probe"));
  aster_ui_shell_pump_idle();assert(pvis && !fvis && writes==1 && !stored[0]);return 0;
 }
 assert(!aster_ui_shell_theme_request_id("missing"));
 aster_ui_shell_show_page(1);aster_ui_shell_pump_idle();assert(page==1);
 aster_ui_shell_step_page(-1);assert(steps==-1);
 aster_ui_shell_power_home();assert(page==0);
 uint32_t a,b,c;aster_launcher_art_stats(&a,&b,&c);assert(a==1 && b==2 && c==3);
 assert(aster_ui_shell_theme_request_id("flux"));
 aster_ui_shell_pump_idle();assert(!writes);
 panel->apply(1);
 contacts=true;aster_ui_shell_pump_idle();assert(fvis && !writes);contacts=false;
 boot_busy=true;aster_ui_shell_pump_idle();assert(fvis && !writes);boot_busy=false;
 off=true;aster_ui_shell_pump_idle();assert(fvis && !writes);off=false;
 pages=true;aster_ui_shell_pump_idle();assert(fvis && !writes);pages=false;
 exit_open=true;aster_ui_shell_pump_idle();assert(fvis && !writes);exit_open=false;
 power=true;aster_ui_shell_pump_idle();assert(fvis && !writes);power=false;
 closing=true;aster_ui_shell_pump_idle();assert(fvis && !writes);closing=false;
 aster_ui_shell_theme_set_blocked(true);aster_ui_shell_pump_idle();assert(fvis && !writes);
 aster_ui_shell_theme_set_blocked(false);
 fail=true;aster_ui_shell_pump_idle();assert(fvis && !pvis && !strcmp(stored,"flux"));
 fail=false;
 for(unsigned i=0;i<100;i++) {
  aster_ui_shell_theme_request_next();aster_ui_shell_pump_idle();
  assert(pvis && !fvis && !strcmp(stored,"probe"));
  aster_launcher_art_stats(&a,&b,&c);assert(!a && !b && !c);
  aster_ui_shell_theme_request_next();aster_ui_shell_pump_idle();
  assert(fvis && !pvis && !strcmp(stored,"flux"));
 }
 aster_ui_theme_stats_t snapshot={0};
 aster_ui_shell_theme_stats(&snapshot);
 assert(snapshot.attempts==201 && snapshot.switches==200 && snapshot.failures==1);
 assert(snapshot.rollbacks==1 && !snapshot.kv_failures && !snapshot.remount_failures);
 assert(!strcmp(snapshot.active_id,"flux") && snapshot.visible && !snapshot.pending_id);
 assert(aster_ui_shell_theme_request_id("probe"));
 aster_ui_shell_hide();assert(!fvis && !pvis && panel->count()==1);
 aster_ui_shell_power_home();assert(exits==1);
 aster_ui_shell_show();aster_ui_shell_pump_idle();assert(fvis && !pvis);
 aster_launcher_slider_stats_t stats={0};
 aster_ui_shell_slider_stats(&stats);assert(stats.value==42);
 return 0;
}
"""


class UiShellTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which("cc"), "缺少 C 编译器")
    def test_switch_transaction(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            for name, source in STUBS.items():
                (directory / name).write_text(source, encoding="utf-8")
            # 将真实实现复制到桩目录，使引号 include 先匹配桩，而非固件头。
            for name in ("ui_shell.c", "ui_shell.h", "ui_theme.h", "launcher.h"):
                shutil.copyfile(UI / name, directory / name)
            fixture = directory / "fixture.c"
            fixture.write_text(FIXTURE, encoding="utf-8")
            binary = directory / "fixture"
            subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-I", str(directory), "-I", str(UI.parent),
                            str(directory / "ui_shell.c"), str(fixture),
                            "-o", str(binary)], check=True)
            for scenario in ("switch", "restore", "invalid", "drain", "frame", "kv"):
                subprocess.run([str(binary), scenario], check=True)


if __name__ == "__main__":
    unittest.main()

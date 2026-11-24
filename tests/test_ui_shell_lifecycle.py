"""验证公共桌面生命周期辅助函数的调用顺序及引导分支。"""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
UI = ROOT / "systems/zephyr/ui/launcher"

STUBS = {
    "aster_device.h": """#include <stdbool.h>
int aster_device_display_count(void);
int aster_device_display_orientation_get(void);
int aster_device_display_orientation_set(int);
void aster_device_audio_ui_release(void);
""",
    "aster_platform.h": "#define ASTER_ORIENT_PORTRAIT 0\n",
    "ui_lvgl.h": """#include <stddef.h>
#include <stdint.h>
void *aster_ui_lvgl_screen(void);
void *aster_ui_lvgl_indev(void);
void aster_ui_lvgl_pointer(uint32_t, uint32_t, int);
void aster_ui_lvgl_resync_window(void);
void lv_obj_clean(void *);
void lv_indev_reset(void *, void *);
void lv_indev_wait_release(void *);
""",
    "ui_splash.h": "void aster_splash_boot(void);\nvoid aster_splash_tick(void);\n",
    "qpanel.h": "void aster_qpanel_force_close(void);\n",
    "gesture.h": "void aster_gesture_reset(void);\n",
    "touch_ui.h": """struct aster_touch_ui_state { int unused; };
void aster_touch_ui_reset(struct aster_touch_ui_state *);
""",
    "setup_ui.h": "#include <stdbool.h>\nbool aster_setup_ui_tick(void);\n",
}

FIXTURE = r"""
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include "ui_shell_lifecycle.h"
#include "touch_ui.h"

static char trace[128];
static unsigned n;
static int display, orient, setup;
static struct aster_touch_ui_state touch;
static int screen, indev;
static uint32_t px, py;
static void hit(char c) { trace[n++] = c; trace[n] = 0; }
static void expect(const char *s)
{
    assert(strcmp(trace, s) == 0);
    n = 0;
    trace[0] = 0;
}
int aster_device_display_count(void) { hit('D'); return display; }
int aster_device_display_orientation_get(void) { hit('O'); return orient; }
int aster_device_display_orientation_set(int o) { hit('S'); orient = o; return 0; }
void aster_device_audio_ui_release(void) { hit('A'); }
void *aster_ui_lvgl_screen(void) { return &screen; }
void *aster_ui_lvgl_indev(void) { return &indev; }
void aster_ui_lvgl_pointer(uint32_t x, uint32_t y, int pressed)
{ assert(!pressed); px=x; py=y; hit('P'); }
void aster_ui_lvgl_resync_window(void) { hit('W'); }
void lv_obj_clean(void *obj) { assert(obj == &screen); hit('C'); }
void lv_indev_reset(void *dev, void *obj)
{ assert(dev == &indev && obj == NULL); hit('I'); }
void lv_indev_wait_release(void *dev) { assert(dev == &indev); hit('R'); }
void aster_qpanel_force_close(void) { hit('Q'); }
void aster_gesture_reset(void) { hit('G'); }
void aster_touch_ui_reset(struct aster_touch_ui_state *s)
{ assert(s == &touch); hit('T'); }
void aster_splash_boot(void) { hit('B'); }
void aster_splash_tick(void) { hit('K'); }
bool aster_setup_ui_tick(void) { hit('F'); return setup != 0; }

int main(int argc, char **argv)
{
    const bool visible = argc > 1 && atoi(argv[1]) != 0;
    orient = 1;
    aster_ui_shell_prepare_show(&touch); expect("TQGOSW");
    aster_ui_shell_prepare_show(&touch); expect("TQGOW");
    aster_ui_shell_clear_for_show(); expect("CPI");
    assert(px == 0 && py == 0);
    aster_ui_shell_prepare_hide(&touch, 32, 48); expect("RPTQ");
    assert(px == 32 && py == 48);
    aster_ui_shell_clear_for_hide(); expect("GAC");
    assert(aster_ui_shell_boot_frame_consumed(visible)); expect("D");
    assert(aster_ui_shell_boot_busy());
    display = 1; setup = 1;
#ifdef CONFIG_ASTER_FIRST_BOOT
    assert(aster_ui_shell_boot_frame_consumed(visible));
    expect(visible ? "DFK" : "DBFK");
    assert(aster_ui_shell_boot_frame_consumed(false)); expect("DFK");
    setup = 0;
    assert(!aster_ui_shell_boot_frame_consumed(false)); expect("DF");
#else
    assert(!aster_ui_shell_boot_frame_consumed(visible));
    expect(visible ? "D" : "DB");
    assert(!aster_ui_shell_boot_frame_consumed(false)); expect("D");
#endif
    assert(!aster_ui_shell_boot_busy());
    return 0;
}
"""


class UiShellLifecycleTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which("cc"), "缺少 C 编译器")
    def test_lifecycle_order(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            for name, source in STUBS.items():
                (directory / name).write_text(source, encoding="utf-8")
            fixture = directory / "fixture.c"
            fixture.write_text(FIXTURE, encoding="utf-8")
            for first_boot in (False, True):
                binary = directory / "fixture"
                flags = ["-DCONFIG_ASTER_FIRST_BOOT=1"] if first_boot else []
                subprocess.run(
                    ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                     *flags, "-I", str(directory), "-I", str(UI),
                     str(UI / "ui_shell_lifecycle.c"), str(fixture),
                     "-o", str(binary)], check=True)
                for visible in ("0", "1"):
                    subprocess.run([str(binary), visible], check=True)


if __name__ == "__main__":
    unittest.main()

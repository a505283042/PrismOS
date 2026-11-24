"""公共 UI Shell 的转发契约：使用独立桩验证，不依赖 LVGL 或板卡。"""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
UI = ROOT / "systems/zephyr/ui/launcher"


class UiShellTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which("cc"), "缺少 C 编译器")
    def test_forwarding(self):
        fixture = r"""
#include <assert.h>
#include "ui_shell.h"
static unsigned calls;
static int arg;
int aster_launcher_init(void) { calls |= 1u; return -7; }
void aster_launcher_show(void) { calls |= 2u; }
void aster_launcher_request_show(void) { calls |= 4u; }
void aster_launcher_show_page(int page) { calls |= 8u; arg = page; }
void aster_launcher_step_page(int dir) { calls |= 16u; arg = dir; }
void aster_launcher_hide(void) { calls |= 32u; }
void aster_launcher_pump_idle(void) { calls |= 64u; }
uint32_t aster_launcher_idle_wait_ms(void) { calls |= 128u; return 37u; }
void aster_launcher_art_stats(uint32_t *a, uint32_t *b, uint32_t *c)
{ calls |= 256u; *a=1; *b=2; *c=3; }
void aster_launcher_slider_stats(aster_launcher_slider_stats_t *out)
{ calls |= 512u; out->value=42; }
int main(void)
{
    uint32_t a=0,b=0,c=0;
    aster_launcher_slider_stats_t stats={0};
    assert(aster_ui_shell_init()==-7);
    aster_ui_shell_show();
    aster_ui_shell_request_show();
    aster_ui_shell_show_page(1); assert(arg==1);
    aster_ui_shell_step_page(-1); assert(arg==-1);
    aster_ui_shell_hide();
    aster_ui_shell_pump_idle();
    assert(aster_ui_shell_idle_wait_ms()==37u);
    aster_ui_shell_art_stats(&a,&b,&c);
    assert(a==1 && b==2 && c==3);
    aster_ui_shell_slider_stats(&stats); assert(stats.value==42);
    assert(calls==1023u);
    return 0;
}
"""
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / "fixture.c"
            exe = Path(tmp) / "fixture"
            src.write_text(fixture, encoding="utf-8")
            subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-I", str(UI), str(UI / "ui_shell.c"), str(src),
                            "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    unittest.main()

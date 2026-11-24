"""S31 LCD Flush stage D: passive per-phase timing and ownership guards.

Static only: ESP32-S31 timing must be validated on physical hardware.
The diagnostic never changes QSPI clock, bounce buffer, RGB565 order, or
synchronous DMA completion semantics.
"""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
LCD_ROOT = ROOT / "boards/fakepod_s31/idf/main"
LCD = (LCD_ROOT / "board_lcd.c").read_text(encoding="utf-8")
CMAKE = (LCD_ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
UI = (ROOT / "systems/espidf/components/aster_ui/ui.c").read_text(encoding="utf-8")


class LCDProfile(unittest.TestCase):
    def test_measurement_only_inside_ui_bench(self):
        self.assertIn("static atomic_bool lcd_profile_enabled;", LCD)
        self.assertIn("void aster_idf_lcd_profile_begin(void)", LCD)
        self.assertIn("void aster_idf_lcd_profile_end(bool report)", LCD)
        self.assertIn("atomic_exchange(&lcd_profile_enabled, false)", LCD)
        self.assertIn("const bool measure = atomic_load(&lcd_profile_enabled);", LCD)
        self.assertIn("aster_idf_lcd_profile_begin(void) __attribute__((weak))", UI)
        self.assertIn("aster_idf_lcd_profile_end(bool report) __attribute__((weak))", UI)
        warm = UI[UI.index("if (bench_warm && now >= bench_warm)"):
                  UI.index("if (now >= bench_step)")]
        self.assertLess(warm.index("perf_reset();"),
                        warm.index("aster_idf_lcd_profile_begin();"))
        finished = UI[UI.index("if (now >= bench_end)"):
                      UI.index("if (bench_warm && now >= bench_warm)")]
        self.assertIn("aster_idf_lcd_profile_end(true)", finished)
        self.assertIn("perf_print();", finished)
        interrupted = UI[UI.index("void aster_idf_ui_before_app(void)"):
                         UI.index("void aster_idf_ui_after_app(void)")]
        self.assertIn("aster_idf_lcd_profile_end(false)", interrupted)

    def test_snapshot_swap_submit_wait_are_independent(self):
        self.assertIn("lcd_profile.copy_us +=", LCD)
        self.assertIn("lcd_profile.swap_us +=", LCD)
        self.assertIn("lcd_profile.submit_us +=", LCD)
        self.assertIn("lcd_profile.wait_us +=", LCD)
        self.assertIn("lcd_profile.snapshot_bytes +=", LCD)
        self.assertIn("lcd_profile.wire_bytes +=", LCD)
        self.assertIn("lcd_profile.rects++;", LCD)
        self.assertIn("lcd_profile.stripes++;", LCD)
        self.assertIn("lcd_profile.max_wait_us", LCD)
        self.assertIn("lcd_profile.errors++;", LCD)
        self.assertIn("LCD PROFILE rects=", LCD)
        self.assertIn("copy_us=%llu swap_us=%llu submit_us=%llu wait_us=%llu", LCD)
        self.assertIn("wait_max_us=%lu errors=%lu\\n", LCD)
        self.assertIn("wait_us is elapsed blocking time including RTOS wake", LCD)

    def test_lcd_production_pipeline_preserved(self):
        self.assertIn("cfg.pclk_hz = 40000000;", LCD)
        self.assertIn("cfg.trans_queue_depth = 1;", LCD)
        self.assertIn("FP_LCD_WIDTH * 16 * 2", LCD)
        self.assertIn("for (uint32_t row = 0; row < h; row += 16)", LCD)
        self.assertIn("dst[col] = src[col + 1];", LCD)
        self.assertIn("dst[col + 1] = src[col];", LCD)
        self.assertIn("esp_lcd_panel_draw_bitmap(panel,", LCD)
        self.assertIn("xSemaphoreTake(completed, portMAX_DELAY);", LCD)
        self.assertIn("memcpy(dst, src, w * 2);", LCD)
        self.assertNotIn("esp_async_color_convert", LCD)
        self.assertNotIn("esp_private/dma2d.h", LCD)

    def test_component_dependencies(self):
        self.assertEqual(CMAKE.count("        esp_timer\n"), 2)
        self.assertIn('"board_lcd.c"', CMAKE)


if __name__ == "__main__":
    unittest.main()

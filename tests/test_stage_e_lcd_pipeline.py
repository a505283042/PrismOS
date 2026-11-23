"""S31 stage E: 16-row ping-pong LCD DMA experiment ownership guards.

Static contract checks only. Real A/B performance, physical color correctness
and DMA completion safety require on-board ESP-IDF 6.1 validation.
"""
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LCD = (ROOT / "boards/fakepod_s31/idf/main/board_lcd.c").read_text(encoding="utf-8")
HEADER = (ROOT / "boards/fakepod_s31/idf/main/board_lcd.h").read_text(encoding="utf-8")
UI = (ROOT / "systems/espidf/components/aster_ui/ui.c").read_text(encoding="utf-8")


class StageEPipeline(unittest.TestCase):
    def test_double_buffer_is_bench_only(self):
        self.assertIn("LCD_STRIP_ROWS = 16", LCD)
        self.assertIn("static uint16_t *pipe_bounce;", LCD)
        self.assertIn("MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL", LCD)
        self.assertIn("void aster_idf_lcd_pipe_set_bench(bool enabled)", HEADER)
        self.assertIn("void aster_idf_lcd_pipe_set_bench(bool enabled)", LCD)
        self.assertIn("pipe_requested = false;", LCD)
        self.assertIn("heap_caps_free(pipe_bounce);", LCD)
        self.assertIn('LCD PIPE fallback=single reason=internal_dma_ram', LCD)
        self.assertIn("cfg.pclk_hz = 40000000;", LCD)
        self.assertIn("cfg.trans_queue_depth = 1;", LCD)
        self.assertIn("return transfer_snapshot(x, y, right - x, bottom - y);", LCD)

    def test_one_inflight_no_reuse_until_completion(self):
        pipeline = LCD[LCD.index("static int transfer_snapshot_pipe("):
                       LCD.index("int fakepod_lcd_present(")]
        self.assertIn("uint16_t *buffers[2] = {bounce, pipe_bounce};", pipeline)
        self.assertIn("slot ^= 1u;", pipeline)
        submit = pipeline.index("esp_lcd_panel_draw_bitmap(")
        prepare_next = pipeline.index("prepare_pipe_strip(buffers[slot ^ 1u]")
        wait = pipeline.index("xSemaphoreTake(completed, pdMS_TO_TICKS(1000))")
        change_slot = pipeline.index("slot ^= 1u;")
        self.assertLess(submit, prepare_next)
        self.assertLess(prepare_next, wait)
        self.assertLess(wait, change_slot)
        self.assertIn("if (!next_rows)\n            break;", pipeline)
        self.assertNotIn("portMAX_DELAY", pipeline)
        self.assertIn("goto fail_unknown_dma;", pipeline)
        self.assertIn("pipe_fault = true;", pipeline)

    def test_fail_closed_cannot_free_or_keep_transmitting_unknown_dma(self):
        self.assertIn("if (!pipe_fault && pipe_bounce)", LCD)
        self.assertIn("pipe_fault || !w || !h", LCD)
        self.assertIn("if (!panel_io || !panel || pipe_fault)", LCD)
        self.assertIn("blocked until reboot to preserve buffer ownership", LCD)
        self.assertIn("if (pipe_requested && pipe_enabled)", LCD)
        self.assertIn("if (pipe_requested && atomic_load(&lcd_profile_enabled))", LCD)

    def test_ui_ab_and_abort_guards(self):
        self.assertIn('!strcmp(argv[2], "bench-pipe")', UI)
        self.assertIn("request = 400 + (int)seconds;", UI)
        self.assertIn("if (request >= 400) seconds = request - 400;", UI)
        self.assertIn("ui_bench_pipe = request >= 400 && request < 500;", UI)
        self.assertIn("aster_idf_lcd_pipe_set_bench(ui_bench_pipe);", UI)
        self.assertIn("aster_idf_lcd_pipe_set_bench(false);", UI)
        self.assertIn("ui_bench_pipe = false;", UI)
        self.assertIn("atomic_compare_exchange_strong(&ui_bench_reserved", UI)
        complete = UI[UI.index("if (now >= bench_end)"):
                      UI.index("if (bench_warm && now >= bench_warm)")]
        self.assertLess(complete.index("aster_idf_lcd_profile_end(true);"),
                        complete.index("aster_idf_lcd_pipe_set_bench(false);"))
        abort = UI[UI.index("void aster_idf_ui_before_app(void)"):
                   UI.index("void aster_idf_ui_after_app(void)")]
        self.assertIn("aster_idf_lcd_profile_end(false);", abort)
        self.assertIn("aster_idf_lcd_pipe_set_bench(false);", abort)
        self.assertIn("ui_bench_pipe = false;", abort)


if __name__ == "__main__":
    unittest.main()

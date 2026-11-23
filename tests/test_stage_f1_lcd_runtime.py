"""Stage F1 S31 runtime LCD pipeline source-contract tests (not hardware validation)."""
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LCD = (ROOT / "boards/fakepod_s31/idf/main/board_lcd.c").read_text(encoding="utf-8")
HDR = (ROOT / "boards/fakepod_s31/idf/main/board_lcd.h").read_text(encoding="utf-8")
UI = (ROOT / "systems/espidf/components/aster_ui/ui.c").read_text(encoding="utf-8")


class StageF1RuntimePipeline(unittest.TestCase):
    def test_board_preserves_bench_and_fail_closed(self):
        self.assertIn("void aster_idf_lcd_pipe_set_mode(bool enabled)", LCD)
        self.assertIn("void aster_idf_lcd_pipe_set_bench(bool enabled)", LCD)
        self.assertIn("aster_idf_lcd_pipe_set_mode(enabled);", LCD)
        self.assertIn("void aster_idf_lcd_pipe_status(bool *requested, bool *enabled, bool *fault)", HDR)
        self.assertIn("MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL", LCD)
        self.assertIn("if (!pipe_fault && pipe_bounce)", LCD)
        self.assertIn("pipe_fault = true;", LCD)
        self.assertIn("pipe_fault || !w || !h", LCD)
        self.assertIn("cfg.trans_queue_depth = 1;", LCD)
        self.assertIn("cfg.pclk_hz = 40000000;", LCD)

    def test_console_uses_mailbox_only(self):
        command = UI[UI.index("int aster_idf_ui_command("):
                     UI.index("static void ui_pipe_poll(")]
        self.assertIn('!strcmp(argv[2], "pipe")', command)
        for mode in ("on", "off", "status"):
            self.assertIn(f'!strcmp(argv[3], "{mode}")', command)
        self.assertIn("atomic_compare_exchange_strong(&ui_pipe_request", command)
        self.assertNotIn("aster_idf_lcd_pipe_set_mode(", command)
        worker = UI[UI.index("static void ui_pipe_poll("):UI.index("static void perf_tick(")]
        self.assertIn("atomic_exchange(&ui_pipe_request, 0)", worker)
        self.assertIn("aster_idf_lcd_pipe_set_mode(action == 1);", worker)
        self.assertIn("ui_pipe_runtime = enabled && !fault;", worker)
        self.assertIn("aster_idf_lcd_pipe_set_mode(false);", worker)

    def test_owner_polls_both_launcher_and_app(self):
        app_tick = UI[UI.index("static void app_tick("):UI.index("int aster_idf_ui_init(")]
        idle = UI[UI.index("void aster_idf_ui_idle("):UI.index("void aster_idf_ui_before_app(")]
        self.assertIn("ui_pipe_poll();", app_tick)
        self.assertIn("ui_pipe_poll();", idle)
        self.assertLess(idle.index("ui_pipe_poll();"), idle.index("aster_deploy_ui_tick()"))

    def test_default_on_is_owner_only_and_has_safe_fallback(self):
        init = UI[UI.index("int aster_idf_ui_init("):UI.index("void aster_idf_ui_idle(")]
        self.assertLess(init.index("aster_idf_lcd_pipe_set_mode(true);"),
                        init.index("aster_launcher_init();"))
        self.assertIn("ui_pipe_runtime = enabled && !fault;", init)
        self.assertIn("aster_idf_lcd_pipe_set_mode(false);", init)
        self.assertIn("UI PIPE boot default=ON", init)
        self.assertIn("reason=hooks_unavailable", init)
        self.assertIn("#if !defined(ASTER_IDF_HOST_TEST) && defined(CONFIG_IDF_TARGET_ESP32S31)", init)
        self.assertIn("static bool ui_pipe_runtime;", UI)

    def test_bench_override_and_app_handoff_preserve_choice(self):
        self.assertIn("aster_idf_lcd_pipe_set_bench(ui_bench_pipe);", UI)
        self.assertEqual(UI.count("aster_idf_lcd_pipe_set_bench(ui_pipe_runtime);"), 2)
        self.assertIn("UI BENCH refused: LCD runtime mode change pending", UI)
        self.assertIn("UI PIPE refused: a UI benchmark is running", UI)
        self.assertIn("static bool ui_pipe_runtime;", UI)
        worker = UI[UI.index("static void ui_pipe_poll("):UI.index("static void perf_tick(")]
        self.assertNotIn("nvs_set", worker)


if __name__ == "__main__":
    unittest.main()

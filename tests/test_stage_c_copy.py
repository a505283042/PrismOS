"""S31 Stage C: diagnostic-only PPA Fill and CPU/DMA2D copy guards.

These checks do not claim device speed, power or ESP-IDF compilation. The
actual comparison must be run on S31 with an external power meter.
"""
import unittest
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
RENDER=ROOT/"systems/espidf/components/aster_render"
FILL=(RENDER/"ppa_fill.c").read_text(encoding="utf-8")
FILL_H=(RENDER/"ppa_fill.h").read_text(encoding="utf-8")
BENCH=(RENDER/"stage_c_bench.c").read_text(encoding="utf-8")
CMAKE=(RENDER/"CMakeLists.txt").read_text(encoding="utf-8")
UI=(ROOT/"systems/espidf/components/aster_ui/ui.c").read_text(encoding="utf-8")
GFX=(ROOT/"systems/zephyr/ui/ui_gfx.c").read_text(encoding="utf-8")

class StageC(unittest.TestCase):
    def test_ppa_initializes_and_validates_both_lvgl_and_app_area(self):
        self.assertIn("aster_idf_ppa_fill_init();",UI)
        self.assertIn("static bool self_test(void)",FILL)
        self.assertIn("lv_draw_buf_init(",FILL)
        self.assertIn("TEST_W=128, TEST_H=64",FILL)
        self.assertIn("aster_idf_ppa_fill_rgb565(large,TEST_W,TEST_H,16,8,96,48",FILL)
        self.assertIn("large[y*TEST_W+x]!=want",FILL)
        self.assertIn("bool aster_idf_ppa_fill_ready(void)",FILL_H)
        self.assertIn("if (!self_test())",FILL)
        self.assertIn("aster_idf_ppa_fill_reset();",FILL)

    def test_only_accelerate_large_aligned_rgb565(self):
        self.assertIn("PPA_OPERATION_FILL",FILL)
        self.assertIn("PPA_TRANS_MODE_BLOCKING",FILL)
        self.assertIn("(uint64_t)w * h < 4096",FILL)
        self.assertIn("((uintptr_t)data & 63)",FILL)
        self.assertIn("PPA_FILL_COLOR_MODE_RGB565",FILL)
        self.assertIn("PPA_UNIT",FILL)
        self.assertIn("if (!s_out_be && aster_idf_ppa_fill_rgb565(",GFX)

    def test_fill_cpu_vs_ppa_ui_ab_is_diagnostic_and_restored(self):
        header=(RENDER/"ppa_fill.h").read_text(encoding="utf-8")
        self.assertIn("aster_idf_ppa_fill_set_bench_cpu_only",header)
        self.assertIn("static bool bench_cpu_only;",FILL)
        self.assertIn("if (failed || bench_cpu_only || task->type",FILL)
        self.assertIn("if (!client || failed || bench_cpu_only",FILL)
        self.assertIn('!strcmp(argv[2], "bench-cpu")',UI)
        self.assertIn('!strcmp(argv[2], "bench-ppa")',UI)
        self.assertIn('request = 200 + (int)seconds;',UI)
        self.assertIn('request = 300 + (int)seconds;',UI)
        self.assertIn('ui_bench_cpu_only = request >= 200 && request < 300;',UI)
        self.assertIn('aster_idf_ppa_fill_set_bench_cpu_only(ui_bench_cpu_only);',UI)
        self.assertIn('UI BENCH RESULT fill_mode=%s',UI)
        finish=UI[UI.index('if (now >= bench_end)'):UI.index('if (bench_warm && now >= bench_warm)')]
        self.assertLess(finish.index("perf_print();"),
                        finish.index("aster_idf_ppa_fill_set_bench_cpu_only(false);"))
        self.assertIn("ui_bench_cpu_only = false;",finish)
        self.assertIn("atomic_store(&ui_bench_reserved,false)",finish)
        before_app=UI[UI.index("void aster_idf_ui_before_app(void)"):
                      UI.index("void aster_idf_ui_after_app(void)")]
        self.assertIn("int pending=atomic_load(&perf_request);",before_app)
        self.assertIn("aster_idf_ppa_fill_set_bench_cpu_only(false);",before_app)
        self.assertIn("atomic_store(&ui_bench_reserved,false)",before_app)
        self.assertIn("UI BENCH aborted: application launched",before_app)

    def test_dma2d_is_diagnostic_only_and_public(self):
        self.assertIn('if(CONFIG_IDF_TARGET_ESP32S31)',CMAKE)
        self.assertIn('"stage_c_bench.c"',CMAKE)
        self.assertIn("esp_driver_dma fatfs",CMAKE)
        self.assertIn('if defined(CONFIG_IDF_TARGET_ESP32S31)',BENCH)
        self.assertIn('#include "esp_async_color_convert.h"',BENCH)
        self.assertIn("esp_async_color_convert_install_dma2d",BENCH)
        self.assertIn("esp_color_convert_blocking",BENCH)
        self.assertIn("ESP_COLOR_FOURCC_RGB16",BENCH)
        self.assertNotIn("esp_private/dma2d.h",BENCH)
        self.assertNotIn("stage_c_bench",GFX)
        self.assertNotIn("stage_c_bench",FILL)

    def test_gated_audio_idle_and_external_power(self):
        self.assertIn('!strcmp(argv[2], "stagec")',UI)
        self.assertIn('!strcmp(argv[2], "stagec-power")',UI)
        self.assertIn("if (app_active)",UI)
        self.assertIn("aster_player_service_info(&player)",UI)
        self.assertIn("player.state==ASTER_PLAYER_PLAYING",UI)
        self.assertIn("player.state==ASTER_PLAYER_PAUSED",UI)
        self.assertIn("aster_idf_ppa_fill_ready()",UI)
        self.assertIn("xTaskCreate(bench_worker",BENCH)
        self.assertIn("atomic_compare_exchange_strong(&running",BENCH)
        self.assertIn("STAGEC BENCH edge=",BENCH)
        self.assertIn("pixel_match=%d",BENCH)
        self.assertIn("STAGEC POWER begin",BENCH)
        self.assertIn("STAGEC POWER end",BENCH)
        self.assertIn("current_ma=EXTERNAL_ONLY",BENCH)
        self.assertIn("vTaskDelay(pdMS_TO_TICKS(1))",BENCH)
        self.assertIn("memcpy(ctx->dst,ctx->src,size);",BENCH)
        # Blocking wrapper signals its semaphore in ISR before recycling
        # the DMA2D transaction: a single-entry pool can race the next call.
        self.assertIn(".backlog=2",BENCH)
        self.assertIn("status!=ESP_ERR_INVALID_STATE",BENCH)
        self.assertIn("ctx->dma_retries++",BENCH)
        self.assertIn("attempt==2u",BENCH)
        self.assertIn("verdict=%s",BENCH)
        self.assertIn('valid?"VALID":"INVALID"',BENCH)
        self.assertIn("ctx->src[0]=(uint16_t)(ctx->src[0]+1u)",BENCH)
        self.assertIn("*(volatile uint16_t *)ctx->dst",BENCH)
        self.assertIn("sample_checksum",BENCH)
        self.assertIn("pixel_errors",BENCH)
        self.assertIn("bool aster_idf_stage_c_running(void)",BENCH)
        self.assertIn("atomic_load(&running)",BENCH)
        self.assertIn("atomic_load(&ui_bench_reserved)",UI)
        self.assertIn("aster_idf_stage_c_running()",UI)
        self.assertIn("atomic_compare_exchange_strong(&ui_bench_reserved",UI)
        self.assertIn("atomic_store(&ui_bench_reserved,false)",UI)
        self.assertIn("STAGEC refused: UI BENCH still running",UI)
        self.assertIn("UI BENCH refused: Stage C CPU/DMA2D worker running",UI)

if __name__=="__main__":
    unittest.main()

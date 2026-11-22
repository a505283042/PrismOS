"""Static ABI/ownership regression for S31 JPEG+PPA cover fast path.

No hardware simulation: validates that the optional ABI remains installable
on old firmware, with limits/rollback intact. Hardware colour and throughput
must still be verified by an actual ESP32-S31 build and on-device tests.
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
FW = ROOT / "systems/espidf/components"
HW = (FW / "aster_render/hifi_cover_accel.c").read_text(encoding="utf-8")
H = (FW / "aster_render/hifi_cover_accel.h").read_text(encoding="utf-8")
RT = (FW / "aster_runtime/images.c").read_text(encoding="utf-8")
CM = (FW / "aster_render/CMakeLists.txt").read_text(encoding="utf-8")
API = (ROOT / "contract/include/aster/img.h").read_text(encoding="utf-8")


class StageB(unittest.TestCase):
    def test_v2_shares_v1_import_and_prefix(self):
        self.assertIn("#define ASTER_IMG_COVER_VERSION 2u", API)
        self.assertIn("aster_img_cover_render_args_t", API)
        self.assertIn("uint32_t src_offset, src_length, dim_off, dim_span;", API)
        self.assertIn("aster_img_render(const char *path", API)
        self.assertNotIn('"aster_img_cover"', RT)
        self.assertIn('"aster_img_render", (void *)render', RT)
        self.assertIn("if (args.version == ASTER_IMG_COVER_VERSION)", RT)
        self.assertIn("args.version != 1", RT)

    def test_wasm_boundaries_and_atomic_publication(self):
        for part in ("v2.dst_span < required", "v2.dim_span < required",
                     "(uint64_t)v2.dst_off + required <= v2.dim_off",
                     "guest(env, v2.dst_off", "guest(env, v2.dim_off",
                     "aster_idf_resolve_path("):
            self.assertIn(part, RT)
        self.assertIn("uint16_t *guest_original", HW)
        self.assertIn("uint16_t *guest_dim", HW)
        self.assertLess(HW.index("esp_err_t e = jpeg_decoder_process("),
                        HW.index("memcpy(guest_original +"))
        self.assertLess(HW.index("dim_using_ppa(scaled, dark, pitch, height)"),
                        HW.index("memcpy(guest_original +"))
        self.assertIn("No transient cover", HW)

    def test_memory_and_audio_io_limits(self):
        self.assertIn("MAX_JPEG_COMPRESSED = 2 * 1024 * 1024", HW)
        self.assertIn("MAX_JPEG_PIXELS = 2100000", HW)
        self.assertIn("(uint64_t)src_offset + src_length > size", HW)
        self.assertIn("if (burst > 8192u) burst = 8192u;", HW)
        self.assertIn("vTaskDelay(1)", HW)
        self.assertIn("heap_caps_aligned_alloc(ALIGNMENT", HW)
        self.assertIn("jpeg_alloc_decoder_mem(", HW)
        self.assertIn("jpeg_decoder_process(", HW)
        self.assertIn("heap_caps_free(decoded)", HW)

    def test_ppa_blend_and_cpu_fallback(self):
        for key in ("PPA_OPERATION_BLEND", "PPA_BLEND_COLOR_MODE_A8",
                    "ppa_do_blend(", "dim_using_cpu(", "DIM_ALPHA = 151"):
            self.assertIn(key, HW)
        self.assertIn("dim_using_cpu(scaled, dark", HW)
        self.assertIn("PPA_DIM : ASTER_HIFI_COVER_CPU_DIM", HW)
        self.assertIn("#if defined(CONFIG_IDF_TARGET_ESP32S31)", H)

    def test_s31_only_build_and_image_worker(self):
        self.assertIn("if(CONFIG_IDF_TARGET_ESP32S31)", CM)
        self.assertIn("REQUIRES esp_timer heap log esp_driver_ppa esp_driver_jpeg fatfs", CM)
        # Dependency expansion precedes Kconfig, so it must remain outside
        # the S31 target branch even though only that board compiles the C file.
        self.assertNotIn("ASTER_HIFI_COVER_DEPS", CM)
        self.assertLess(CM.index("endif()"), CM.index("REQUIRES esp_timer heap log esp_driver_ppa esp_driver_jpeg fatfs"))
        self.assertIn("hifi_cover_accel.c", CM)
        self.assertLess(RT.index('#include "sdkconfig.h"'), RT.index('#include "hifi_cover_accel.h"'))


if __name__ == "__main__":
    unittest.main()

"""P1.3.2 生命周期和诊断源码边界测试。"""
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
UI = ROOT / "systems/zephyr/ui/launcher"

class ThemeStability(unittest.TestCase):
    def test_hide_reason(self):
        flux = (UI / "flux.c").read_text()
        shell = (UI / "ui_shell.c").read_text()
        self.assertIn("sound_return = reason == ASTER_UI_HIDE_APP;", flux)
        self.assertIn("flux_hide(ASTER_UI_HIDE_REFRESH);", flux)
        self.assertIn("active()->hide(ASTER_UI_HIDE_SWITCH);", shell)
        self.assertIn("active()->hide(ASTER_UI_HIDE_APP);", shell)

    def test_native_audio_gate(self):
        audio = (ROOT / "platform/esp32s3/audio.c").read_text()
        start = audio.index("int aster_plat_audio_ui_play(")
        end = audio.index("void aster_plat_audio_ui_stop(", start)
        play = audio[start:end]
        self.assertLess(play.index("aster_idf_native_audio_owned()"), play.index("wake_player()"))
        self.assertIn("return -EAGAIN;", play)
        port = (ROOT / "systems/espidf/adapters/cs43131_i2s_pdm/port.c").read_text()
        self.assertIn("return atomic_load(&native_owner);", port)

    def test_phase_order(self):
        shell = (UI / "ui_shell.c").read_text()
        phases = ["before_hide", "after_hide", "after_init", "after_show", "after_frame", "after_kv"]
        positions = [shell.index('observe("' + phase + '")') for phase in phases]
        self.assertEqual(positions, sorted(positions))

    def test_stress_owner(self):
        ui = (ROOT / "systems/espidf/components/aster_ui/ui.c").read_text()
        command = ui[ui.index("int aster_idf_ui_command("):ui.index("static void ui_pipe_poll(")]
        self.assertIn("theme_stress_request", command)
        self.assertNotIn("theme_stress_poll(", command)
        self.assertIn("theme_stress_poll(theme_blocked);", ui)
        self.assertIn("stress_end(\"low_internal_heap\")", ui)

if __name__ == "__main__":
    unittest.main()

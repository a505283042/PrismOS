"""公共设置边界、旧配置兼容及调度顺序检查。"""
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
UI = ROOT / "systems/zephyr/ui/launcher"


class UiShellSettingsTest(unittest.TestCase):
    def test_settings_owner(self):
        flux = (UI / "flux.c").read_text(encoding="utf-8")
        shell = (UI / "ui_shell_settings.c").read_text(encoding="utf-8")
        for name in ("brightness_pending", "timeout_index", "screen_timeouts"):
            self.assertNotIn(name, flux)
            self.assertIn(name, shell)
        for name in ("aster_qpanel_bind_theme", "aster_qpanel_restore_volume",
                     "aster_wifi_settings_tick", "aster_qpanel_tick",
                     "aster_power_schedule", "aster_wifi_settings_open",
                     "aster_bluetooth_settings_open"):
            self.assertNotIn(name + "(", flux)
            self.assertIn(name + "(", shell)

    def test_legacy_settings(self):
        flux = (UI / "flux.c").read_text(encoding="utf-8")
        shell = (UI / "ui_shell_settings.c").read_text(encoding="utf-8")
        self.assertIn('#define LEGACY_SETTINGS_KV "aster.launcher.flux"', shell)
        self.assertIn('"screen-timeout", &timeout_index, 1', shell)
        self.assertIn("CLAMP(value, 25, 100)", shell)
        self.assertIn("{0, 30000, 60000, 120000, 300000}", shell)
        self.assertIn("aster_ui_shell_settings_restore_brightness(s[5]);", flux)
        self.assertIn("selected[3], aster_ui_shell_settings_brightness(), motion, quiet", flux)
        self.assertLess(flux.index('aster_service_kv_set(KV_ID, "state"'),
                        flux.index("aster_ui_shell_settings_save_timeout();"))

    def test_panel_and_brightness_order(self):
        shell = (UI / "ui_shell_settings.c").read_text(encoding="utf-8")
        flux = (UI / "flux.c").read_text(encoding="utf-8")
        self.assertLess(shell.index("aster_qpanel_restore_volume();"),
                        shell.index("brightness_pending = brightness;"))
        self.assertLess(shell.index("aster_wifi_settings_tick();"),
                        shell.index("aster_qpanel_tick();"))
        self.assertLess(flux.index("aster_ui_shell_settings_flush_brightness();"),
                        flux.index("aster_ui_shell_volume_flush();"))

    def test_build_wiring(self):
        for path in ("systems/zephyr/ui/CMakeLists.txt",
                     "systems/espidf/components/aster_ui/CMakeLists.txt",
                     "systems/espidf/tests/check_ui.py"):
            self.assertIn("launcher/ui_shell_settings.c",
                          (ROOT / path).read_text(encoding="utf-8"), path)


if __name__ == "__main__":
    unittest.main()

"""检查公共应用目录、系统状态及音量逻辑的边界。"""
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
UI = ROOT / "systems/zephyr/ui/launcher"


class UiShellStateTest(unittest.TestCase):
    def test_catalog_boundary(self):
        flux = (UI / "flux.c").read_text(encoding="utf-8")
        shell = (UI / "ui_shell_state.c").read_text(encoding="utf-8")
        for name in ("app_find", "app_count", "app_snapshot", "app_start",
                     "app_uninstall", "registry_callback"):
            self.assertNotIn("aster_service_" + name + "(", flux)
            self.assertIn("aster_service_" + name + "(", shell)
        self.assertIn("atomic_store_explicit(&catalog_dirty, true", shell)
        self.assertIn("aster_ui_shell_catalog_dirty() && !modal", flux)

    def test_volume_boundary(self):
        flux = (UI / "flux.c").read_text(encoding="utf-8")
        shell = (UI / "ui_shell_state.c").read_text(encoding="utf-8")
        self.assertNotIn("aster_device_audio_set_volume(", flux)
        self.assertNotIn("static int volume_pending", flux)
        self.assertIn("aster_ui_shell_volume_discard_pending();", flux)
        self.assertIn("aster_ui_shell_volume_flush();", flux)
        self.assertLess(shell.index("aster_device_audio_set_volume(volume_pending)"),
                        shell.index('aster_service_kv_set("aster.qpanel", "volume"'))
        self.assertIn("volume_pending = -1;", shell)

    def test_build_wiring(self):
        for path in ("systems/zephyr/ui/CMakeLists.txt",
                     "systems/espidf/components/aster_ui/CMakeLists.txt",
                     "systems/espidf/tests/check_ui.py"):
            self.assertIn("launcher/ui_shell_state.c",
                          (ROOT / path).read_text(encoding="utf-8"), path)


if __name__ == "__main__":
    unittest.main()

"""公共输入及电源状态的源码边界与调用顺序检查。"""
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
UI = ROOT / "systems/zephyr/ui/launcher"


class UiShellInputTest(unittest.TestCase):
    def test_single_owner(self):
        shell = (UI / "ui_shell_input.c").read_text(encoding="utf-8")
        flux = (UI / "flux.c").read_text(encoding="utf-8")
        for state in ("static aster_power_idle_t power_idle;",
                      "static struct aster_touch_ui_state launcher_touch_ui;"):
            self.assertIn(state, shell)
            self.assertNotIn(state, flux)
        for name in ("touch", "tick", "resume"):
            self.assertNotIn("aster_launcher_power_" + name + "(void)\n{", flux)
        self.assertIn("aster_ui_shell_input_poll(touch, flux_reset_drag);", flux)

    def test_touch_routing_order(self):
        shell = (UI / "ui_shell_input.c").read_text(encoding="utf-8")
        poll = shell.split("void aster_ui_shell_input_poll(", 1)[1].split(
            "void aster_ui_shell_power_bind(", 1)[0]
        calls = ("aster_device_input_poll(", "aster_touch_ui_begin_batch(",
                 "aster_launcher_power_touch(", "aster_touch_ui_accept(",
                 "aster_gesture_reset();", "reset_drag();",
                 "lv_indev_wait_release(", "route(&events[i]);",
                 "aster_device_input_keys_poll(")
        positions = [poll.index(call) for call in calls]
        self.assertEqual(positions, sorted(positions))
        self.assertIn("continue;", poll.split("aster_launcher_power_touch(", 1)[1]
                      .split("aster_touch_ui_accept(", 1)[0])

    def test_build_wiring(self):
        for path in ("systems/zephyr/ui/CMakeLists.txt",
                     "systems/espidf/components/aster_ui/CMakeLists.txt",
                     "systems/espidf/tests/check_ui.py"):
            self.assertIn("launcher/ui_shell_input.c",
                          (ROOT / path).read_text(encoding="utf-8"), path)


if __name__ == "__main__":
    unittest.main()

"""Regression guard for the FLUX APP-exit master-volume bug.

FLUX quiet mode only gates UI sounds. It must never force global audio
volume to zero when an APP exits: media sessions survive UI transitions.
This source-contract test complements the ESP-IDF build and hardware test.
"""
from pathlib import Path
import unittest

SRC = (
    Path(__file__).resolve().parents[1]
    / "systems/zephyr/ui/launcher/flux.c"
)


def section(source, begin, end):
    first = source.index(begin)
    last = source.index(end, first + len(begin))
    return source[first:last]


class FluxVolumeOwnership(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = SRC.read_text(encoding="utf-8")

    def test_return_to_launcher_does_not_mute_music(self):
        show = section(
            self.source, "static void flux_show(void)",
            "static void flux_hide(void)")
        self.assertIn("aster_ui_shell_volume_discard_pending();", show)
        self.assertNotIn("volume_pending = 0;", show)
        self.assertNotIn("aster_device_audio_set_volume(", show)
        # Restoring a previously persisted volume at boot is separate.
        self.assertIn("aster_ui_shell_settings_on_show();", show)
        settings = SRC.with_name("ui_shell_settings.c").read_text(encoding="utf-8")
        self.assertIn("aster_qpanel_restore_volume();", settings)

    def test_quiet_setting_only_affects_ui_sound_effects(self):
        action = section(
            self.source, "} else if (a == ACT_QUIET) {",
            "} else if (a == ACT_MOTION)")
        self.assertIn("quiet = !quiet;", action)
        self.assertIn("aster_device_audio_ui_stop();", action)
        self.assertNotIn("volume_pending =", action)
        self.assertNotIn("aster_device_audio_set_volume(", action)
        self.assertNotIn("saved_volume", action)
        sound = section(
            self.source, "static void sound_play(int which, int speed)",
            "static lv_obj_t *box(")
        self.assertIn("if (quiet ||", sound)

    def test_volume_slider_remains_the_master_volume_controller(self):
        slider = section(
            self.source, "static void slider_changed(lv_event_t *e)",
            "static void slider_card(")
        self.assertIn("aster_ui_shell_volume_changed(v,", slider)
        state = SRC.with_name("ui_shell_state.c").read_text(encoding="utf-8")
        tick = section(
            state, "    if (volume_pending >= 0) {",
            "    if (volume_save) {")
        self.assertIn("aster_device_audio_set_volume(volume_pending)", tick)


if __name__ == "__main__":
    unittest.main()

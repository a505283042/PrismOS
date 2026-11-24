/* SPDX-License-Identifier: MIT */
#include "ui_shell_lifecycle.h"
#include "aster_device.h"
#include "aster_platform.h"
#include "ui_lvgl.h"
#include "ui_splash.h"
#include "qpanel.h"
#include "gesture.h"
#include "touch_ui.h"
#if defined(CONFIG_ASTER_FIRST_BOOT)
#include "setup_ui.h"
#endif

void aster_ui_shell_prepare_show(struct aster_touch_ui_state *touch)
{
    aster_touch_ui_reset(touch);
    aster_qpanel_force_close();
    aster_gesture_reset();
    if (aster_device_display_orientation_get() != ASTER_ORIENT_PORTRAIT)
        (void)aster_device_display_orientation_set(ASTER_ORIENT_PORTRAIT);
    aster_ui_lvgl_resync_window();
}

void aster_ui_shell_clear_for_show(void)
{
    lv_obj_clean(aster_ui_lvgl_screen());
    aster_ui_lvgl_pointer(0, 0, 0);
    lv_indev_reset(aster_ui_lvgl_indev(), NULL);
}

void aster_ui_shell_prepare_hide(struct aster_touch_ui_state *touch,
                                 uint32_t x, uint32_t y)
{
    lv_indev_wait_release(aster_ui_lvgl_indev());
    aster_ui_lvgl_pointer(x, y, false);
    aster_touch_ui_reset(touch);
    aster_qpanel_force_close();
}

/* 主题仍须在 prepare_hide 与 clear_for_hide 之间保存自身状态。
 * 屏幕清理后才由主题释放图标及绘制缓存，保持原有所有权顺序。 */
void aster_ui_shell_clear_for_hide(void)
{
    aster_gesture_reset();
    aster_device_audio_ui_release();
    lv_obj_clean(aster_ui_lvgl_screen());
}

static bool boot_busy;

bool aster_ui_shell_boot_busy(void) { return boot_busy; }

bool aster_ui_shell_boot_frame_consumed(bool desktop_visible)
{
    static bool boot_image_started;

    boot_busy = true;
    if (!aster_device_display_count())
        return true;
    if (!boot_image_started) {
        boot_image_started = true;
        if (!desktop_visible)
            aster_splash_boot();
    }
#if defined(CONFIG_ASTER_FIRST_BOOT)
    if (aster_setup_ui_tick()) {
        aster_splash_tick();
        return true;
    }
#endif
    boot_busy = false;
    return false;
}

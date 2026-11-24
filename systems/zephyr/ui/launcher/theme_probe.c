/* SPDX-License-Identifier: MIT */
/* P1.3 验证布局：只验证独立桌面、APP 生命周期和主题切换，不是正式第二主题。 */
#include "ui_theme.h"
#include "ui_shell.h"
#include "ui_shell_input.h"
#include "ui_shell_lifecycle.h"
#include "ui_shell_settings.h"
#include "ui_shell_state.h"
#include "ui_lvgl.h"
#include "ui_font.h"
#include "gesture.h"
#include <string.h>

static lv_obj_t *root, *status;
static bool ready, want_show, launch_pending, launch_queued;
static char app_id[64];

static void set_status(const char *text)
{
    if (status) {
        aster_ui_font_warm(16, text);
        lv_label_set_text(status, text);
    }
}

static void on_switch(lv_event_t *event)
{
    (void)event;
    aster_ui_shell_theme_request_id("flux");
}

static void on_launch(lv_event_t *event)
{
    (void)event;
    if (app_id[0] && !launch_queued)
        launch_pending = true;
}

static lv_obj_t *label(lv_obj_t *parent, const char *text, unsigned px, uint32_t color)
{
    lv_obj_t *obj = lv_label_create(parent);
    if (!obj)
        return NULL;
    lv_obj_set_style_text_font(obj, aster_ui_font(px), 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    aster_ui_font_warm(px, text);
    lv_label_set_text(obj, text);
    lv_obj_set_width(obj, lv_pct(94));
    lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(obj, LV_LABEL_LONG_MODE_DOTS);
    return obj;
}

static bool add_button(const char *text, int y, lv_event_cb_t callback)
{
    lv_obj_t *button = lv_button_create(root);
    if (!button)
        return false;
    lv_obj_set_size(button, lv_pct(84), 48);
    lv_obj_align(button, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x244c70), 0);
    lv_obj_t *caption = label(button, text, 16, 0xffffff);
    if (!caption)
        return false;
    lv_obj_center(caption);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, NULL);
    return true;
}

static bool probe_show(void)
{
    if (ready)
        return true;
    lv_obj_t *screen = aster_ui_lvgl_screen();
    if (!screen)
        return false;
    aster_ui_shell_input_prepare_show();
    aster_ui_fonts_prepare(aster_ui_lvgl_width(), aster_ui_lvgl_height(), "Theme probe");
    aster_ui_shell_clear_for_show();
    root = lv_obj_create(screen);
    if (!root)
        return false;
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, lv_pct(100), lv_pct(100));
    lv_obj_set_pos(root, 0, 0);
    lv_obj_set_style_bg_color(root, lv_color_hex(0x112538), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *title = label(root, "PrismOS / Theme probe", 20, 0xffffff);
    if (!title)
        return false;
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 28);
    if (!add_button("Return to FLUX", 76, on_switch))
        return false;
    aster_service_app_t app = {0};
    app_id[0] = 0;
    if (aster_ui_shell_catalog_snapshot(0, &app)) {
        app.name[sizeof(app.name) - 1] = 0;
        memcpy(app_id, app.id, sizeof(app_id) - 1);
        app_id[sizeof(app_id) - 1] = 0;
        if (!add_button(app.name, 134, on_launch))
            return false;
    }
    status = label(root, "Swipe down: quick panel", 16, 0xbcdaf2);
    if (!status)
        return false;
    lv_obj_align(status, LV_ALIGN_TOP_MID, 0, 196);
    ready = true;
    launch_pending = launch_queued = false;
    aster_ui_shell_catalog_ack();
    aster_ui_shell_settings_on_show();
    aster_ui_shell_volume_discard_pending();
    return true;
}

static void probe_hide(aster_ui_hide_reason_t reason)
{
    (void)reason;
    if (root) {
        aster_ui_shell_input_prepare_hide();
        aster_ui_shell_clear_for_hide();
    }
    root = status = NULL;
    ready = false;
    launch_pending = launch_queued = false;
}

static void probe_touch(const aster_touch_evt_t *event)
{
    bool pressed = event->action != ASTER_TOUCH_UP;
    aster_gest_t gesture = aster_gesture_feed(event->x, event->y, pressed,
        aster_qpanel_allow(true), aster_ui_lvgl_height());
    aster_ui_lvgl_pointer(event->x, event->y, pressed);
    if (aster_qpanel_on_gesture(gesture, ASTER_QPANEL_OVER_LAUNCHER))
        lv_indev_wait_release(aster_ui_lvgl_indev());
}

static void probe_reset_drag(void) {}
static int probe_init(void) { return 0; }
static void probe_request_show(void) { want_show = true; }
static void probe_page(int page) { (void)page; }
static void probe_step(int dir) { (void)dir; }
static bool probe_visible(void) { return ready; }
static bool probe_can_switch(void) { return ready && !launch_pending && !launch_queued; }
static uint32_t probe_wait(void) { return aster_ui_shell_power_is_off() ? 25 : 10; }
static void probe_power_error(void) { set_status("Power request failed"); }

static void probe_tick(void)
{
    if (aster_ui_shell_boot_frame_consumed(ready))
        return;
    if (!ready && want_show) {
        want_show = false;
        if (!probe_show()) {
            probe_hide(ASTER_UI_HIDE_REFRESH);
            /* Shell 在隐藏的验证布局恢复失败时执行回退。 */
            aster_ui_shell_theme_report_mount_failure();
            return;
        }
    }
    if (!ready)
        return;
    if (!aster_ui_shell_power_is_off() && aster_ui_fonts_poll())
        lv_obj_invalidate(aster_ui_lvgl_screen());
    if (aster_ui_shell_catalog_dirty() && !aster_qpanel_is_open() && !launch_queued &&
        !aster_ui_shell_input_contacts_down()) {
        probe_hide(ASTER_UI_HIDE_REFRESH);
        if (!probe_show()) {
            probe_hide(ASTER_UI_HIDE_REFRESH);
            aster_ui_shell_theme_report_mount_failure();
            return;
        }
    }
    aster_ui_shell_input_poll(probe_touch, probe_reset_drag);
    if (aster_ui_shell_power_is_off()) {
        aster_launcher_power_tick();
        if (aster_ui_shell_power_is_off())
            return;
    }
    if (launch_pending) {
        launch_pending = false;
        launch_queued = aster_ui_shell_app_start(app_id) == 0;
        if (!launch_queued)
            set_status("APP launch failed");
    }
    aster_ui_shell_settings_flush_brightness();
    aster_ui_shell_volume_flush();
    aster_ui_shell_settings_tick();
    if (!aster_ui_shell_power_is_off())
        aster_ui_lvgl_pump();
}

static void probe_palette(aster_qpanel_palette_t *out)
{
    *out = (aster_qpanel_palette_t){
        .bg = 0x112538, .fg = 0xffffff, .dim = 0xbcdaf2,
        .surface = 0x244c70, .accent = 0x79c6ff};
}

const aster_ui_theme_t aster_ui_theme_probe = {
    .id = "probe", .name = "Probe",
    .init = probe_init, .show = probe_show, .hide = probe_hide,
    .request_show = probe_request_show, .show_page = probe_page,
    .step_page = probe_step, .tick = probe_tick,
    .idle_wait_ms = probe_wait, .is_visible = probe_visible,
    .can_switch = probe_can_switch, .palette = probe_palette,
    .power_error = probe_power_error,
};

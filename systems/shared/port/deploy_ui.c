/* SPDX-License-Identifier: MIT. Shared serial-deployment overlay, UI worker only. */
#include "aster_deploy_ui.h"
#include "aster_deploy.h"
#include "aster_device.h"
#include "aster_os.h"
#include "ui_lvgl.h"
#include "ui_font.h"
#include "touch_ui.h"
#include "gesture.h"
#include <stdio.h>
static lv_obj_t *root, *title, *stage, *bar, *detail, *clock_label, *button;
static aster_deploy_snapshot shown;
static struct aster_touch_ui_state touch;
static uint64_t last_draw;

static lv_obj_t *label(int y, unsigned px)
{
    lv_obj_t *o = lv_label_create(root);
    lv_obj_set_pos(o, 16, y);
    lv_obj_set_width(o, aster_ui_lvgl_layout_width() - 32);
    lv_obj_set_style_text_font(o, aster_ui_font(px), 0);
    lv_obj_set_style_text_color(o, lv_color_hex(0x203048), 0);
    return o;
}

static void dismiss(lv_event_t *e)
{
    (void)e;
    aster_deploy_dismiss(shown.generation);
}

void aster_deploy_ui_close(void)
{
    if (root) {
        lv_obj_delete(root);
        root = NULL;
        aster_ui_lvgl_pointer(0, 0, 0);
        aster_gesture_reset();
        aster_touch_ui_reset(&touch);
    }
}

bool aster_deploy_ui_tick(void)
{
    aster_deploy_snapshot s;
    if (!aster_deploy_get(&s)) {
        aster_deploy_ui_close();
        return false;
    }
    if (!root) {
        aster_ui_lvgl_resync_window();
        int w = aster_ui_lvgl_layout_width(), h = aster_ui_lvgl_layout_height();
        if (!aster_ui_lvgl_screen())
            return false;
        root = lv_obj_create(lv_layer_top());
        lv_obj_remove_style_all(root);
        lv_obj_set_size(root, w, h);
        lv_obj_set_style_bg_color(root, lv_color_hex(0xf2f5fa), 0);
        lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
        lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
        bool short_screen = h < 300;
        int top = (h > 320 ? (h - 320) / 2 : 0);
        title = label(top + (short_screen ? 12 : 20), 20);
        stage = label(top + (short_screen ? 48 : 62), 16);
        bar = lv_bar_create(root);
        lv_obj_set_pos(bar, 16, top + (short_screen ? 82 : 102));
        lv_obj_set_size(bar, w - 32, 12);
        lv_bar_set_range(bar, 0, 1000);
        detail = label(top + (short_screen ? 104 : 134), 12);
        lv_obj_set_height(detail, short_screen ? 36 : 72);
        lv_label_set_long_mode(detail, LV_LABEL_LONG_MODE_WRAP);
        clock_label = label(top + (short_screen ? 148 : 210), 12);
        button = lv_button_create(root);
        lv_obj_set_pos(button, 16, h - 58);
        lv_obj_set_size(button, w - 32, 38);
        lv_obj_add_event_cb(button, dismiss, LV_EVENT_CLICKED, NULL);
        lv_obj_t *text = lv_label_create(button);
        lv_obj_set_style_text_font(text, aster_ui_font(16), 0);
        lv_label_set_text(text, "返回桌面");
        lv_obj_center(text);
        aster_touch_ui_reset(&touch);
        last_draw = 0;
    }
    uint64_t now = aster_os_uptime_ms();
    if (now - last_draw >= 100 || shown.phase != s.phase || shown.generation != s.generation) {
        static const char *names[] = {"",
                                      "正在接收文件",
                                      "正在校验文件",
                                      "文件接收完成，等待安装",
                                      "正在检查安装包",
                                      "正在复制程序和资源",
                                      "正在同步文件",
                                      "正在提交安装结果",
                                      "安装成功",
                                      "安装失败"};
        bool result = s.phase == ASTER_DEPLOY_DONE || s.phase == ASTER_DEPLOY_ERROR;
        bool transfer_error = s.error && (s.failed_phase == ASTER_DEPLOY_RECEIVE ||
                                          s.failed_phase == ASTER_DEPLOY_VERIFY);
        lv_label_set_text(
            title, result ? (s.error ? (transfer_error ? "传输失败" : "安装失败") : "安装成功")
                          : "串口传输与安装");
        lv_label_set_text(stage, names[s.phase]);
        char line[240];
        unsigned percent =
            s.total ? (unsigned)((s.done > s.total ? s.total : s.done) * 100 / s.total) : 0;
        snprintf(line, sizeof(line), "%u%%  %llu / %llu KiB\n%.120s", percent,
                 (unsigned long long)(s.done / 1024), (unsigned long long)(s.total / 1024), s.file);
        lv_label_set_text(detail, line);
        if (s.error)
            snprintf(line, sizeof(line), "错误 %d  ·  用时 %u 秒", s.error,
                     (unsigned)((now - s.since_ms) / 1000));
        else
            snprintf(line, sizeof(line), "用时 %u 秒", (unsigned)((now - s.since_ms) / 1000));
        lv_label_set_text(clock_label, line);
        lv_bar_set_value(bar, s.phase == ASTER_DEPLOY_DONE ? 1000 : (int)(percent * 10),
                         LV_ANIM_OFF);
        if (result || s.phase == ASTER_DEPLOY_RECEIVED)
            lv_obj_remove_flag(button, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(button, LV_OBJ_FLAG_HIDDEN);
        shown = s;
        last_draw = now;
    }
    aster_touch_evt_t events[16];
    lv_indev_t *pointer = aster_ui_lvgl_indev();
    for (unsigned batch = 0; batch < 4; batch++) {
        int n = aster_device_input_poll(events, 16);
        if (n <= 0)
            break;
        uint32_t masks[16];
        for (int i = 0; i < n; i++)
            masks[i] = events[i].contact_mask;
        aster_touch_ui_begin_batch(&touch, masks, (uint8_t)n);
        for (int i = 0; i < n; i++) {
            struct aster_touch_ui_decision d = aster_touch_ui_accept(
                &touch, (uint8_t)i, events[i].action, events[i].slot, events[i].contact_mask);
            if (d.release_pointer)
                aster_ui_lvgl_pointer(0, 0, 0);
            if (d.route)
                aster_ui_lvgl_pointer(events[i].x, events[i].y, events[i].action != 3);
            if (pointer)
                lv_indev_read(pointer);
        }
    }
    if (aster_ui_fonts_poll())
        lv_obj_invalidate(root);
    aster_ui_lvgl_request_frame();
    aster_ui_lvgl_pump();
    return true;
}

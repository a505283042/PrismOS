/* SPDX-License-Identifier: MIT */
#ifndef ASTER_UI_THEME_H
#define ASTER_UI_THEME_H

#include <stdbool.h>
#include <stdint.h>
#include "qpanel.h"

/* 静态注册的原生桌面主题。所有回调只在原 UI 所有者执行。
 * show 返回 false 时，hide 必须能清理部分创建的对象。
 * tick 沿用各主题原有帧节奏；无独立任务或常驻帧缓冲。 */
typedef enum {
    ASTER_UI_HIDE_APP,
    ASTER_UI_HIDE_SWITCH,
    ASTER_UI_HIDE_REFRESH,
} aster_ui_hide_reason_t;

typedef struct {
    const char *id;
    const char *name;
    int (*init)(void);
    bool (*show)(void);
    void (*hide)(aster_ui_hide_reason_t reason);
    void (*request_show)(void);
    void (*show_page)(int page);
    void (*step_page)(int dir);
    void (*tick)(void);
    uint32_t (*idle_wait_ms)(void);
    bool (*is_visible)(void);
    bool (*can_switch)(void);
    void (*palette)(aster_qpanel_palette_t *out);
    void (*art_stats)(uint32_t *bakes, uint32_t *hits, uint32_t *cached_bytes);
    void (*power_error)(void);
} aster_ui_theme_t;

extern const aster_ui_theme_t aster_ui_theme_flux;
extern const aster_ui_theme_t aster_ui_theme_probe;

#endif

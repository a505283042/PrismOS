/* SPDX-License-Identifier: MIT */
#ifndef ASTER_UI_SHELL_SETTINGS_H
#define ASTER_UI_SHELL_SETTINGS_H

#include <stdbool.h>
#include <stdint.h>
#include <lvgl.h>
#include "qpanel.h"

/* 公共设置由现有 UI 所有者执行。保留旧 KV 键与写入时序。 */
void aster_ui_shell_settings_load_timeout(void);
void aster_ui_shell_settings_save_timeout(void);
uint32_t aster_ui_shell_settings_timeout_ms(void);
const char *aster_ui_shell_settings_timeout_name(void);
void aster_ui_shell_settings_next_timeout(void);
int aster_ui_shell_settings_brightness(void);
void aster_ui_shell_settings_restore_brightness(uint8_t value);
void aster_ui_shell_settings_sync_brightness(void);
void aster_ui_shell_settings_request_brightness(int value);
void aster_ui_shell_settings_flush_brightness(void);

/* 快捷面板及连接设置仍由原组件绘制、管理覆盖层与 APP 快照。 */
void aster_ui_shell_settings_bind_panel(const aster_qpanel_theme_ops_t *ops);
void aster_ui_shell_settings_on_show(void);
void aster_ui_shell_settings_tick(void);
bool aster_ui_shell_settings_panel_open(void);
bool aster_ui_shell_settings_pages_open(void);
bool aster_ui_shell_settings_bluetooth_available(void);
void aster_ui_shell_settings_open_wifi(lv_obj_t *parent);
void aster_ui_shell_settings_open_bluetooth(lv_obj_t *parent);
void aster_ui_shell_settings_request_exit(void);
int aster_ui_shell_settings_schedule_power(uint32_t mode);

#endif

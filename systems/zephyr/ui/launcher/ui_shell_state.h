/* SPDX-License-Identifier: MIT */
#ifndef ASTER_UI_SHELL_STATE_H
#define ASTER_UI_SHELL_STATE_H

#include "launcher.h"
#include "aster_services.h"
#include "aster_device.h"
#include "aster_os.h"
#include "aster_power.h"

/* 使用原应用注册表的拥有型记录，不复制整份目录或持有内部指针。
 * 注册表通知仅置位；由 UI 所有者在安全点刷新主题。 */
void aster_ui_shell_catalog_init(void);
bool aster_ui_shell_catalog_dirty(void);
void aster_ui_shell_catalog_ack(void);
int aster_ui_shell_catalog_count(void);
bool aster_ui_shell_catalog_find(const char *id, aster_service_app_t *out);
bool aster_ui_shell_catalog_snapshot(int index, aster_service_app_t *out);
int aster_ui_shell_app_start(const char *id);
int aster_ui_shell_app_uninstall(const char *id);

/* 按需获取现有系统服务状态，不维护另一份状态缓存。 */
bool aster_ui_shell_storage_mounted(void);
int aster_ui_shell_system_info(aster_system_info_t *out);
int aster_ui_shell_wall_clock(aster_time_info_t *out);
int aster_ui_shell_battery(aster_power_battery_t *out);
int aster_ui_shell_wifi_status(aster_wifi_status_t *out);

/* 公共桌面音量滑块的延迟提交与诊断；实际音量以设备服务为准。
 * 快捷面板自身的交互时序不在本阶段修改。 */
int aster_ui_shell_volume_get(void);
int aster_ui_shell_volume_pending(void);
int aster_ui_shell_volume_display(void);
void aster_ui_shell_volume_discard_pending(void);
void aster_ui_shell_volume_pressed(void);
void aster_ui_shell_volume_changed(int value, bool value_changed);
void aster_ui_shell_volume_released(void);
void aster_ui_shell_volume_flush(void);

#endif

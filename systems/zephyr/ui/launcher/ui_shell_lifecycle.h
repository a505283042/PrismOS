/* SPDX-License-Identifier: MIT */
#ifndef ASTER_UI_SHELL_LIFECYCLE_H
#define ASTER_UI_SHELL_LIFECYCLE_H

#include <stdbool.h>
#include <stdint.h>

struct aster_touch_ui_state;

/* 仅由现有 UI 所有者调用；不创建线程、不改变显示提交路径。 */
void aster_ui_shell_prepare_show(struct aster_touch_ui_state *touch);
void aster_ui_shell_clear_for_show(void);
void aster_ui_shell_prepare_hide(struct aster_touch_ui_state *touch,
                                 uint32_t x, uint32_t y);
void aster_ui_shell_clear_for_hide(void);

/* 无显示设备或首次配置占用当前帧时返回 true，调用者跳过桌面更新。 */
bool aster_ui_shell_boot_frame_consumed(bool desktop_visible);
bool aster_ui_shell_boot_busy(void); /* UI 所有者：最近一次引导帧是否被占用 */

#endif

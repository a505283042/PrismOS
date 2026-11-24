/* SPDX-License-Identifier: MIT */
#ifndef ASTER_UI_SHELL_INPUT_H
#define ASTER_UI_SHELL_INPUT_H

#include "aster_device.h"

/* 只在现有 UI 所有者调用；主题回调不持有事件指针。 */
bool aster_ui_shell_input_contacts_down(void);
void aster_ui_shell_input_reset(void);
void aster_ui_shell_input_prepare_show(void);
void aster_ui_shell_input_prepare_hide(void);
void aster_ui_shell_input_poll(void (*route)(const aster_touch_evt_t *),
                               void (*reset_drag)(void));

void aster_ui_shell_power_bind(void);
void aster_ui_shell_power_init(uint32_t timeout_ms, uint8_t brightness,
                               void (*home)(void), void (*error)(void));
bool aster_ui_shell_power_is_off(void);
void aster_ui_shell_power_set_timeout(uint32_t timeout_ms);
void aster_ui_shell_power_set_brightness(uint8_t brightness, bool apply);
int aster_ui_shell_power_screen_off(void);

#endif

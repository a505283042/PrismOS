/* SPDX-License-Identifier: MIT */
/* P1.1: 公共桌面入口。沿用 launcher 的线程及调用契约。 */
#ifndef ASTER_UI_SHELL_H
#define ASTER_UI_SHELL_H
#include "launcher.h"
int aster_ui_shell_init(void);
void aster_ui_shell_show(void);
void aster_ui_shell_request_show(void);
void aster_ui_shell_show_page(int page);
void aster_ui_shell_step_page(int dir);
void aster_ui_shell_hide(void);
void aster_ui_shell_pump_idle(void);
uint32_t aster_ui_shell_idle_wait_ms(void);
void aster_ui_shell_art_stats(uint32_t *bakes, uint32_t *hits, uint32_t *cached_bytes);
void aster_ui_shell_slider_stats(aster_launcher_slider_stats_t *out);
#endif

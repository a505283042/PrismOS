/* SPDX-License-Identifier: MIT */
/* 公共桌面入口；除明确标记的请求外，仅在 UI 所有者执行。 */
#ifndef ASTER_UI_SHELL_H
#define ASTER_UI_SHELL_H
#include "launcher.h"
int aster_ui_shell_init(void);
void aster_ui_shell_show(void);
void aster_ui_shell_request_show(void); /* 跨线程请求 */
void aster_ui_shell_show_page(int page); /* 跨线程请求 */
void aster_ui_shell_step_page(int dir);
void aster_ui_shell_hide(void);
void aster_ui_shell_pump_idle(void);
uint32_t aster_ui_shell_idle_wait_ms(void);
void aster_ui_shell_art_stats(uint32_t *bakes, uint32_t *hits, uint32_t *cached_bytes);
void aster_ui_shell_slider_stats(aster_launcher_slider_stats_t *out);

void aster_ui_shell_bind_panel(void);
void aster_ui_shell_power_home(void);
void aster_ui_shell_power_error(void);
void aster_ui_shell_theme_cancel_pending(void); /* UI owner only */
void aster_ui_shell_theme_request_next(void); /* UI 线程：只排队 */
bool aster_ui_shell_theme_request_id(const char *id); /* 跨线程：只排队 */
const char *aster_ui_shell_theme_id(void); /* 可从诊断线程查询 */
void aster_ui_shell_theme_set_blocked(bool blocked); /* UI 线程 */
void aster_ui_shell_theme_report_mount_failure(void); /* UI 线程 */
void aster_ui_shell_theme_report_display_error(void); /* UI 线程 */

/* 本次启动累计值；仅 UI 所有者读取一致的快照。失败的切换与
 * APP 返回时的重建失败分别计数；同主题请求不计入 attempts。 */
typedef struct {
    uint32_t attempts, switches, failures, rollbacks, remount_failures, kv_failures;
    const char *active_id, *pending_id;
    bool visible, blocked, fault;
} aster_ui_theme_stats_t;
void aster_ui_shell_theme_stats(aster_ui_theme_stats_t *out);

/* 可选诊断观察器，只在 UI 所有者调用，不得改变 UI 状态。 */
void aster_ui_shell_theme_observe(void (*observer)(const char *phase));

#endif

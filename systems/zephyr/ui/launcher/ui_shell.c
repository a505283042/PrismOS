/* SPDX-License-Identifier: MIT */
#include "ui_shell.h"

/* P1.1 仅转发，不改变 FLUX 的状态归属、时序或资源所有权。 */
int aster_ui_shell_init(void) { return aster_launcher_init(); }
void aster_ui_shell_show(void) { aster_launcher_show(); }
void aster_ui_shell_request_show(void) { aster_launcher_request_show(); }
void aster_ui_shell_show_page(int page) { aster_launcher_show_page(page); }
void aster_ui_shell_step_page(int dir) { aster_launcher_step_page(dir); }
void aster_ui_shell_hide(void) { aster_launcher_hide(); }
void aster_ui_shell_pump_idle(void) { aster_launcher_pump_idle(); }
uint32_t aster_ui_shell_idle_wait_ms(void) { return aster_launcher_idle_wait_ms(); }
void aster_ui_shell_art_stats(uint32_t *bakes, uint32_t *hits, uint32_t *cached_bytes)
{
    aster_launcher_art_stats(bakes, hits, cached_bytes);
}
void aster_ui_shell_slider_stats(aster_launcher_slider_stats_t *out)
{
    aster_launcher_slider_stats(out);
}

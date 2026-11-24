/* SPDX-License-Identifier: MIT */
#include "ui_shell.h"
#include "ui_theme.h"
#include "ui_shell_input.h"
#include "ui_shell_lifecycle.h"
#include "ui_shell_settings.h"
#include "ui_shell_state.h"
#include "ui_lvgl.h"
#include "aster_device.h"
#include "aster_power.h"
#include "aster_services.h"
#include "aster_port_util.h"
#include <stdatomic.h>
#include <string.h>

#define THEME_KV "prism.ui"
#define THEME_KEY "theme"
static const aster_ui_theme_t *const themes[] = {&aster_ui_theme_flux, &aster_ui_theme_probe};
static atomic_int active_index;
static atomic_int pending_index = -1;
static atomic_bool show_requested;
static atomic_int page_requested = -1;
static bool preference_loaded, switch_blocked, switch_fault, mount_failed;
static aster_ui_theme_stats_t stats;

static const aster_ui_theme_t *active(void) { return themes[atomic_load(&active_index)]; }

static int find_theme(const char *id)
{
    if (id)
        for (unsigned i = 0; i < ARRAY_SIZE(themes); i++)
            if (!strcmp(id, themes[i]->id))
                return (int)i;
    return -1;
}

const char *aster_ui_shell_theme_id(void) { return active()->id; }

bool aster_ui_shell_theme_request_id(const char *id)
{
    int index = find_theme(id);
    if (index < 0)
        return false;
    atomic_store(&pending_index, index);
    return true;
}

void aster_ui_shell_theme_request_next(void)
{
    atomic_store(&pending_index, (atomic_load(&active_index) + 1) % ARRAY_SIZE(themes));
}

void aster_ui_shell_theme_set_blocked(bool blocked) { switch_blocked = blocked; }
void aster_ui_shell_theme_report_mount_failure(void) { mount_failed = true; }
void aster_ui_shell_theme_report_display_error(void) { switch_fault = true; }

static int panel_count(void)
{
    /* 前台 APP 不展示主题切换行；仍使用当前主题配色。 */
    return active()->is_visible() && !switch_blocked && !switch_fault ? (int)ARRAY_SIZE(themes) : 1;
}

static int panel_index(void) { return atomic_load(&active_index); }
static const char *panel_name(int index)
{
    return index >= 0 && index < (int)ARRAY_SIZE(themes) ? themes[index]->name : themes[0]->name;
}
static void panel_apply(int index)
{
    if (index >= 0 && index < (int)ARRAY_SIZE(themes) && active()->is_visible())
        atomic_store(&pending_index, index);
}
static void panel_palette(aster_qpanel_palette_t *out) { active()->palette(out); }

void aster_ui_shell_bind_panel(void)
{
    static const aster_qpanel_theme_ops_t ops = {
        .count = panel_count, .index = panel_index, .name = panel_name,
        .apply = panel_apply, .palette = panel_palette,
    };
    aster_ui_shell_settings_bind_panel(&ops);
}

void aster_ui_shell_power_home(void)
{
    if (active()->is_visible())
        active()->show_page(0);
    else
        aster_ui_shell_settings_request_exit();
}

void aster_ui_shell_power_error(void)
{
    if (active()->is_visible() && active()->power_error)
        active()->power_error();
}

static bool drain_display(void)
{
    if (aster_device_has(ASTER_DEVICE_METHOD_display_flush_drain_checked))
        return aster_device_display_flush_drain_checked() == 0;
    aster_device_display_flush_drain();
    return true;
}

static bool present_theme(void)
{
    aster_ui_lvgl_request_frame();
    aster_ui_lvgl_pump();
    return drain_display() && !switch_fault;
}

static void persist_theme(void)
{
    const char *id = active()->id;
    if (aster_service_kv_set(THEME_KV, THEME_KEY, id, strlen(id) + 1)) {
        stats.kv_failures++;
        aster_service_logf(2, "theme: %s active for this boot; KV save failed", id);
    }
}

static void restore_preference(void)
{
    if (preference_loaded || aster_ui_shell_boot_busy() || !active()->is_visible() ||
        !aster_ui_shell_storage_mounted())
        return;
    preference_loaded = true;
    char id[32] = {0};
    size_t bytes = 0;
    if (!aster_service_kv_get(THEME_KV, THEME_KEY, id, sizeof(id), &bytes) &&
        bytes > 0 && bytes <= sizeof(id) && id[bytes - 1] == '\0' && strlen(id) + 1 == bytes) {
        int index = find_theme(id);
        if (index >= 0) {
            int empty = -1;
            (void)atomic_compare_exchange_strong(&pending_index, &empty, index);
        } else
            aster_service_logf(2, "theme: unknown saved ID; using FLUX");
    }
}

static void switch_at_safe_point(void)
{
    if (atomic_load(&pending_index) < 0 || switch_blocked || switch_fault || !active()->can_switch() ||
        aster_ui_shell_boot_busy() || aster_ui_shell_input_contacts_down() ||
        aster_ui_shell_power_is_off() ||
        aster_ui_shell_settings_pages_open() || aster_qpanel_exit_is_open() ||
        aster_power_pending() || aster_power_activity_closing())
        return;
    int next = atomic_exchange(&pending_index, -1);
    int previous = atomic_load(&active_index);
    if (next < 0 || next >= (int)ARRAY_SIZE(themes) || next == previous)
        return;
    stats.attempts++;
    /* 先确认旧显示提交完成；不得在 DMA 仍持有资源时清屏。 */
    if (!drain_display()) {
        stats.failures++;
        switch_fault = true;
        aster_service_logf(2, "theme: display drain failed; switching disabled until reboot");
        return;
    }
    active()->hide();
    atomic_store(&active_index, next);
    if (active()->init() || !active()->show()) {
        stats.failures++;
        active()->hide(); /* 包含部分构建失败的清理 */
        atomic_store(&active_index, previous);
        if (active()->init() || !active()->show()) {
            active()->hide();
            atomic_store(&active_index, 0);
            active()->request_show();
            aster_service_logf(2, "theme: previous layout failed; retry FLUX on idle");
            return;
        }
        if (!present_theme()) {
            switch_fault = true;
            aster_service_logf(2, "theme: rollback frame drain failed; reboot required");
            return;
        }
        preference_loaded = true;
        persist_theme();
        stats.rollbacks++;
        aster_service_logf(2, "theme: mount failed; restored %s", active()->id);
        return;
    }
    /* 提交首帧后才保存；若显示后端报错，不再清理可能仍被引用的资源。 */
    if (!present_theme()) {
        stats.failures++;
        switch_fault = true;
        aster_service_logf(2, "theme: first frame drain failed; not saved; reboot required");
        return;
    }
    preference_loaded = true;
    persist_theme();
    stats.switches++;
    aster_service_logf(3, "theme: active=%s", active()->id);
}

/* 启动仍先使用 FLUX，保留首次资源准备及字体预热流程。 */
int aster_ui_shell_init(void) { return themes[0]->init(); }
void aster_ui_shell_show(void) { (void)active()->show(); }
void aster_ui_shell_request_show(void) { atomic_store(&show_requested, true); }
void aster_ui_shell_show_page(int page) { atomic_store(&page_requested, page ? 1 : 0); }
void aster_ui_shell_step_page(int dir) { active()->step_page(dir); }
void aster_ui_shell_hide(void)
{
    atomic_store(&pending_index, -1);
    active()->hide();
}

void aster_ui_shell_pump_idle(void)
{
    if (atomic_exchange(&show_requested, false))
        active()->request_show();
    int page = atomic_exchange(&page_requested, -1);
    if (page >= 0)
        active()->show_page(page);
    active()->tick();
    if (mount_failed) {
        stats.remount_failures++;
        mount_failed = false;
        atomic_store(&pending_index, -1);
        if (!switch_fault && drain_display()) {
            active()->hide();
            atomic_store(&active_index, 0);
            preference_loaded = true;
            if (active()->show() && present_theme()) {
                persist_theme();
                stats.rollbacks++;
                aster_service_logf(2, "theme: remount failed; restored FLUX");
            } else
                active()->request_show();
        } else {
            switch_fault = true;
            aster_service_logf(2, "theme: remount recovery blocked by display fault");
        }
        return;
    }
    restore_preference();
    switch_at_safe_point();
}
uint32_t aster_ui_shell_idle_wait_ms(void) { return active()->idle_wait_ms(); }

void aster_ui_shell_theme_stats(aster_ui_theme_stats_t *out)
{
    if (!out)
        return;
    *out = stats;
    out->active_id = active()->id;
    int pending = atomic_load(&pending_index);
    out->pending_id = pending >= 0 && pending < (int)ARRAY_SIZE(themes) ? themes[pending]->id : NULL;
    out->visible = active()->is_visible();
    out->blocked = switch_blocked;
    out->fault = switch_fault;
}
void aster_ui_shell_art_stats(uint32_t *bakes, uint32_t *hits, uint32_t *cached_bytes)
{
    *bakes = *hits = *cached_bytes = 0;
    if (active()->art_stats)
        active()->art_stats(bakes, hits, cached_bytes);
}
void aster_ui_shell_slider_stats(aster_launcher_slider_stats_t *out)
{
    aster_launcher_slider_stats(out);
}

/* 原 launcher 符号作为兼容入口，不再固定指向 FLUX。 */
int aster_launcher_init(void) { return aster_ui_shell_init(); }
void aster_launcher_show(void) { aster_ui_shell_show(); }
void aster_launcher_hide(void) { aster_ui_shell_hide(); }
void aster_launcher_request_show(void) { aster_ui_shell_request_show(); }
void aster_launcher_show_page(int page) { aster_ui_shell_show_page(page); }
void aster_launcher_step_page(int dir) { aster_ui_shell_step_page(dir); }
void aster_launcher_pump_idle(void) { aster_ui_shell_pump_idle(); }
uint32_t aster_launcher_idle_wait_ms(void) { return aster_ui_shell_idle_wait_ms(); }
void aster_launcher_art_stats(uint32_t *bakes, uint32_t *hits, uint32_t *cached_bytes)
{
    aster_ui_shell_art_stats(bakes, hits, cached_bytes);
}

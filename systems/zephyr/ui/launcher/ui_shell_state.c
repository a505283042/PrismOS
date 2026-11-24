/* SPDX-License-Identifier: MIT */
#include "ui_shell_state.h"
#include "aster_port_util.h"
#include <stdatomic.h>

static atomic_bool catalog_dirty;
static int volume_pending = -1;
static bool volume_save;
static aster_launcher_slider_stats_t slider_stats;

/* 保留原注册表回调符号；回调可来自其他任务，不操作 LVGL。 */
void aster_launcher_on_registry_change(void)
{
    atomic_store_explicit(&catalog_dirty, true, memory_order_relaxed);
}

void aster_ui_shell_catalog_init(void)
{
    aster_service_registry_callback(aster_launcher_on_registry_change);
}

bool aster_ui_shell_catalog_dirty(void)
{
    return atomic_load_explicit(&catalog_dirty, memory_order_relaxed);
}

void aster_ui_shell_catalog_ack(void)
{
    atomic_store_explicit(&catalog_dirty, false, memory_order_relaxed);
}

int aster_ui_shell_catalog_count(void) { return aster_service_app_count(); }
bool aster_ui_shell_catalog_find(const char *id, aster_service_app_t *out)
{
    return aster_service_app_find(id, out);
}
bool aster_ui_shell_catalog_snapshot(int index, aster_service_app_t *out)
{
    return aster_service_app_snapshot(index, out);
}
int aster_ui_shell_app_start(const char *id) { return aster_service_app_start(id); }
int aster_ui_shell_app_uninstall(const char *id) { return aster_service_app_uninstall(id); }

bool aster_ui_shell_storage_mounted(void) { return aster_service_storage_mounted(); }
int aster_ui_shell_system_info(aster_system_info_t *out)
{
    return aster_service_system_info(out);
}
int aster_ui_shell_wall_clock(aster_time_info_t *out) { return aster_os_wall_clock(out); }
int aster_ui_shell_battery(aster_power_battery_t *out) { return aster_power_battery(out); }
int aster_ui_shell_wifi_status(aster_wifi_status_t *out)
{
    return aster_device_wifi_status(out);
}

int aster_ui_shell_volume_get(void) { return aster_device_audio_get_volume(); }
int aster_ui_shell_volume_pending(void) { return volume_pending; }
int aster_ui_shell_volume_display(void)
{
    return volume_pending >= 0 ? volume_pending : aster_ui_shell_volume_get();
}
void aster_ui_shell_volume_discard_pending(void) { volume_pending = -1; }

void aster_ui_shell_volume_pressed(void) { slider_stats.presses++; }
void aster_ui_shell_volume_changed(int value, bool value_changed)
{
    slider_stats.value = value;
    if (value_changed)
        slider_stats.changes++;
    volume_pending = value;
}
void aster_ui_shell_volume_released(void)
{
    volume_save = true;
    slider_stats.releases++;
}

void aster_ui_shell_volume_flush(void)
{
    if (volume_pending >= 0) {
        uint32_t since = aster_os_cycle_us();
        (void)aster_device_audio_set_volume(volume_pending);
        volume_pending = -1;
        uint32_t elapsed = aster_os_cycle_us() - since;
        if (elapsed > slider_stats.max_set_us)
            slider_stats.max_set_us = elapsed;
    }
    if (volume_save) {
        uint8_t value = CLAMP(aster_device_audio_get_volume(), 0, 100);
        uint32_t since = aster_os_uptime32();
        (void)aster_service_kv_set("aster.qpanel", "volume", &value, 1);
        volume_save = false;
        uint32_t elapsed = aster_os_uptime32() - since;
        slider_stats.saves++;
        if (elapsed > slider_stats.max_save_ms)
            slider_stats.max_save_ms = elapsed;
    }
}

void aster_launcher_slider_stats(aster_launcher_slider_stats_t *out)
{
    if (out)
        *out = slider_stats;
}

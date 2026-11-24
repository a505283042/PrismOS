/* SPDX-License-Identifier: MIT */
#include "ui_shell_settings.h"
#include "ui_shell_input.h"
#include "aster_services.h"
#include "aster_power.h"
#include "aster_port_util.h"
#include "wifi_settings.h"

/* 保持原 FLUX 配置的兼容性。亮度字节由原 state 读写入口传入/取出；
 * 主题注册之前不新增迁移键，不改变用户数据及保存频率。 */
#define LEGACY_SETTINGS_KV "aster.launcher.flux"

static int brightness = 90;
static int brightness_pending = -1;
static uint8_t timeout_index;
static const uint32_t screen_timeouts[] = {0, 30000, 60000, 120000, 300000};
static const char *const screen_timeout_names[] = {"永不", "30 秒", "1 分钟", "2 分钟", "5 分钟"};

void aster_ui_shell_settings_load_timeout(void)
{
    size_t bytes = 0;
    uint8_t saved = 0;
    if (!aster_service_kv_get(LEGACY_SETTINGS_KV, "screen-timeout", &saved, 1, &bytes) &&
        bytes == 1 && saved < ARRAY_SIZE(screen_timeouts))
        timeout_index = saved;
}

void aster_ui_shell_settings_save_timeout(void)
{
    (void)aster_service_kv_set(LEGACY_SETTINGS_KV, "screen-timeout", &timeout_index, 1);
}

uint32_t aster_ui_shell_settings_timeout_ms(void)
{
    return screen_timeouts[timeout_index];
}

const char *aster_ui_shell_settings_timeout_name(void)
{
    return screen_timeout_names[timeout_index];
}

void aster_ui_shell_settings_next_timeout(void)
{
    timeout_index = (timeout_index + 1) % ARRAY_SIZE(screen_timeouts);
    aster_ui_shell_power_set_timeout(aster_ui_shell_settings_timeout_ms());
}

int aster_ui_shell_settings_brightness(void) { return brightness; }

void aster_ui_shell_settings_restore_brightness(uint8_t value)
{
    brightness = CLAMP(value, 25, 100);
}

void aster_ui_shell_settings_sync_brightness(void)
{
    aster_ui_shell_power_set_brightness((uint8_t)(brightness * 255 / 100), false);
}

void aster_ui_shell_settings_request_brightness(int value)
{
    brightness = value;
    brightness_pending = value;
}

void aster_ui_shell_settings_flush_brightness(void)
{
    if (brightness_pending >= 0) {
        aster_ui_shell_power_set_brightness((uint8_t)(brightness_pending * 255 / 100), true);
        brightness_pending = -1;
    }
}

void aster_ui_shell_settings_bind_panel(const aster_qpanel_theme_ops_t *ops)
{
    aster_qpanel_bind_theme(ops);
}

void aster_ui_shell_settings_on_show(void)
{
    aster_qpanel_restore_volume();
    brightness_pending = brightness;
}

void aster_ui_shell_settings_tick(void)
{
    aster_wifi_settings_tick();
    aster_qpanel_tick();
}

bool aster_ui_shell_settings_panel_open(void) { return aster_qpanel_is_open(); }
bool aster_ui_shell_settings_pages_open(void)
{
    return aster_wifi_settings_is_open() || aster_bluetooth_settings_is_open();
}
bool aster_ui_shell_settings_bluetooth_available(void)
{
    return aster_bluetooth_settings_available();
}
void aster_ui_shell_settings_open_wifi(lv_obj_t *parent)
{
    aster_wifi_settings_open(parent, NULL);
}
void aster_ui_shell_settings_open_bluetooth(lv_obj_t *parent)
{
    aster_bluetooth_settings_open(parent, NULL);
}
void aster_ui_shell_settings_request_exit(void)
{
    (void)aster_qpanel_request_exit("应用");
}

int aster_ui_shell_settings_schedule_power(uint32_t mode)
{
    aster_power_request_t request = {.size = sizeof(request),
                                     .version = ASTER_POWER_API_VERSION,
                                     .mode = mode};
    if (mode == ASTER_POWER_LIGHT_SLEEP) {
        aster_power_caps_t caps = {0};
        (void)aster_power_caps(&caps);
        request.wake_sources = caps.light_wake & (ASTER_WAKE_TOUCH | ASTER_WAKE_KEY);
    }
    return aster_power_schedule(&request);
}

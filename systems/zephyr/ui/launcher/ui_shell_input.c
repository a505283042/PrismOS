/* SPDX-License-Identifier: MIT */
#include "ui_shell_input.h"
#include "ui_shell_lifecycle.h"
#include "launcher.h"
#include "aster_power.h"
#include "aster_os.h"
#include "aster_port_util.h"
#include "ui_lvgl.h"
#include "gesture.h"
#include "touch_ui.h"

/* 桌面输入和电源状态在同一 UI 所有者线程内维护。
 * APP 前台仍通过旧 launcher_power 兼容入口共享同一电源状态。 */
static struct aster_touch_ui_state launcher_touch_ui;
static uint32_t launcher_touch_x, launcher_touch_y;
static aster_power_idle_t power_idle;
static bool power_contacts_down;
static void (*power_home)(void);
static void (*power_error)(void);

bool aster_ui_shell_input_contacts_down(void) { return power_contacts_down; }

void aster_ui_shell_input_reset(void)
{
    aster_touch_ui_reset(&launcher_touch_ui);
}

void aster_ui_shell_input_prepare_show(void)
{
    aster_ui_shell_prepare_show(&launcher_touch_ui);
}

void aster_ui_shell_input_prepare_hide(void)
{
    aster_ui_shell_prepare_hide(&launcher_touch_ui, launcher_touch_x, launcher_touch_y);
}

void aster_ui_shell_input_poll(void (*route)(const aster_touch_evt_t *),
                               void (*reset_drag)(void))
{
    aster_touch_evt_t events[ASTER_PLAT_TOUCH_EVENT_QUEUE];
    int n = aster_device_input_poll(events, ARRAY_SIZE(events));
    uint32_t masks[ASTER_PLAT_TOUCH_EVENT_QUEUE];
    for (int i = 0; i < n; i++)
        masks[i] = events[i].contact_mask;
    aster_touch_ui_begin_batch(&launcher_touch_ui, masks, (uint8_t)n);
    for (int i = 0; i < n; i++) {
        if (aster_launcher_power_touch(events[i].contact_mask != 0))
            continue;
        struct aster_touch_ui_decision ui =
            aster_touch_ui_accept(&launcher_touch_ui, (uint8_t)i, events[i].action, events[i].slot,
                                  events[i].contact_mask);
        if (ui.reset_gesture) {
            aster_gesture_reset();
            reset_drag();
        }
        if (ui.release_pointer) {
            lv_indev_wait_release(aster_ui_lvgl_indev());
            aster_ui_lvgl_pointer(launcher_touch_x, launcher_touch_y, false);
        }
        if (ui.route) {
            launcher_touch_x = events[i].x;
            launcher_touch_y = events[i].y;
            route(&events[i]);
        }
    }
    aster_key_evt_t keys[ASTER_PLAT_MAX_KEYS];
    while (aster_device_input_keys_poll(keys, ASTER_PLAT_MAX_KEYS) > 0) {
    }
}

void aster_ui_shell_power_bind(void)
{
    aster_power_default_init();
    aster_power_seal();
}

void aster_ui_shell_power_init(uint32_t timeout_ms, uint8_t brightness,
                               void (*home)(void), void (*error)(void))
{
    power_home = home;
    power_error = error;
    aster_power_idle_init(&power_idle, aster_os_uptime_ms(), timeout_ms, brightness);
}

bool aster_ui_shell_power_is_off(void)
{
    return power_idle.screen_off;
}

void aster_ui_shell_power_set_timeout(uint32_t timeout_ms)
{
    power_idle.timeout_ms = timeout_ms;
}

void aster_ui_shell_power_set_brightness(uint8_t brightness, bool apply)
{
    power_idle.brightness = brightness;
    if (apply && !power_idle.screen_off)
        (void)aster_device_backlight_set(true, power_idle.brightness);
}

int aster_ui_shell_power_screen_off(void)
{
    aster_power_caps_t caps = {0};
    (void)aster_power_caps(&caps);
    aster_power_request_t request = {sizeof(request), ASTER_POWER_API_VERSION,
                                     ASTER_POWER_SCREEN_OFF, caps.screen_wake, 0};
    int result = aster_power_transition(&request, power_idle.brightness);
    if (!result)
        power_idle.screen_off = true;
    return result;
}

/* Same owner as Launcher and APP event dispatch. The waking gesture is fully
 * consumed, preventing a wake tap from launching an APP or moving a slider. */
bool aster_launcher_power_touch(bool contacts_down)
{
    power_contacts_down = contacts_down;
    return aster_power_idle_activity(&power_idle, aster_os_uptime_ms(), contacts_down);
}

static void dispatch_power_key(aster_power_key_action_t action)
{
    if (action == ASTER_KEY_ACTION_SHUTDOWN) {
        aster_power_request_t request = {sizeof(request), ASTER_POWER_API_VERSION,
                                         ASTER_POWER_SHUTDOWN, 0, 0};
        if (aster_power_schedule(&request) && power_error)
            power_error();
    } else if (action == ASTER_KEY_ACTION_SCREEN) {
        (void)aster_ui_shell_power_screen_off();
    } else if (action == ASTER_KEY_ACTION_HOME && power_home) {
        power_home();
    }
}

void aster_launcher_power_tick(void)
{
    if (!power_idle.initialized)
        return;

    static struct {
        uint64_t pressed_at;
        uint32_t code;
        bool down, woke, handled;
    } keys[8];

    uint64_t now = aster_os_uptime_ms();
    bool held = power_contacts_down;
    for (unsigned i = 0; i < ARRAY_SIZE(keys); i++)
        held |= keys[i].down;
    (void)aster_power_idle_tick(&power_idle, now, held);
    const aster_power_ops_t *ops = aster_power_ops();
    if (!ops || !ops->map_key)
        return;
    aster_power_key_t events[8];
    int n = aster_power_keys(events, ARRAY_SIZE(events));
    for (int i = 0; i < n; i++) {
        aster_power_key_t *e = &events[i];
        unsigned slot = ARRAY_SIZE(keys), free_slot = ARRAY_SIZE(keys);
        for (unsigned j = 0; j < ARRAY_SIZE(keys); j++) {
            if (keys[j].down && keys[j].code == e->code) {
                slot = j;
                break;
            }
            if (!keys[j].down && free_slot == ARRAY_SIZE(keys))
                free_slot = j;
        }
        if (e->pressed) {
            if (slot != ARRAY_SIZE(keys) || free_slot == ARRAY_SIZE(keys))
                continue;
            keys[free_slot].down = true;
            keys[free_slot].code = e->code;
            keys[free_slot].pressed_at = e->timestamp_ms;
            keys[free_slot].handled = false;
            keys[free_slot].woke = aster_power_idle_activity(&power_idle, e->timestamp_ms, true);
            continue;
        }
        if (slot == ARRAY_SIZE(keys))
            continue;
        keys[slot].down = false;
        bool consumed =
            aster_power_idle_activity(&power_idle, e->timestamp_ms, power_contacts_down);
        if (keys[slot].handled)
            continue;
        uint32_t duration = (uint32_t)(e->timestamp_ms - keys[slot].pressed_at);
        aster_power_key_action_t action = aster_power_map_key(e->code, false, duration);
        if (action == ASTER_KEY_ACTION_SHUTDOWN || (!keys[slot].woke && !consumed))
            dispatch_power_key(action);
    }
    now = aster_os_uptime_ms();
    for (unsigned i = 0; i < ARRAY_SIZE(keys); i++) {
        if (!keys[i].down || keys[i].handled)
            continue;
        uint32_t duration = (uint32_t)(now - keys[i].pressed_at);
        aster_power_key_action_t action = aster_power_map_key(keys[i].code, true, duration);
        if (action != ASTER_KEY_ACTION_NONE) {
            keys[i].handled = true;
            dispatch_power_key(action);
        }
    }
}

void aster_launcher_power_resume(void)
{
    power_idle.screen_off = false;
    power_idle.last_activity_ms = aster_os_uptime_ms();
    power_idle.wake_gesture = true;
    (void)aster_power_screen_on(power_idle.brightness);
}

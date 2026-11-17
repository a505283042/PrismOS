/* SPDX-License-Identifier: MIT */
#ifndef ASTER_POWER_H
#define ASTER_POWER_H
#include <stdbool.h>
#include <stdint.h>
/* Firmware service contract, not the guest ABI. All calls belong to one
 * system/UI owner. Drivers must serialize shared buses internally. Native
 * negative errno throughout. Bind only at boot; tables/context are permanent. */
#define ASTER_POWER_API_VERSION 1u
#define ASTER_POWER_SCREEN_OFF (1u << 0)
#define ASTER_POWER_LIGHT_SLEEP (1u << 1)
#define ASTER_POWER_DEEP_SLEEP (1u << 2)
#define ASTER_POWER_RESTART (1u << 3)
#define ASTER_POWER_SHUTDOWN (1u << 4)
#define ASTER_WAKE_TOUCH (1u << 0)
#define ASTER_WAKE_KEY (1u << 1)
#define ASTER_WAKE_TIMER (1u << 2)
#define ASTER_WAKE_EXTERNAL (1u << 3)
#define ASTER_POWER_BATTERY_PRESENT (1u << 0)
#define ASTER_POWER_EXTERNAL_PRESENT (1u << 1)
#define ASTER_POWER_CHARGING (1u << 2)
#define ASTER_POWER_PERCENT_VALID (1u << 3)
#define ASTER_POWER_VOLTAGE_VALID (1u << 4)
/* Modes are bit values; requests must contain exactly one. Wake masks are
 * per-mode: a touch wake for screen-off does NOT imply deep-sleep support. */
typedef struct {
 uint32_t size,version,modes;
 uint32_t screen_wake,light_wake,deep_wake;
 bool battery,keys;
} aster_power_caps_t;
typedef struct {
 uint32_t size,version,flags,percent,millivolts;
} aster_power_battery_t;
typedef struct {
 uint32_t size,version,mode,wake_sources;
 uint64_t timer_ms;
} aster_power_request_t;
typedef enum {
 ASTER_KEY_ACTION_NONE, ASTER_KEY_ACTION_WAKE, ASTER_KEY_ACTION_SCREEN,
 ASTER_KEY_ACTION_HOME, ASTER_KEY_ACTION_BACK, ASTER_KEY_ACTION_PLAY_PAUSE,
 ASTER_KEY_ACTION_VOLUME_UP, ASTER_KEY_ACTION_VOLUME_DOWN,
 ASTER_KEY_ACTION_POWER_MENU
} aster_power_key_action_t;
typedef struct {
 uint32_t code,pressed;uint64_t timestamp_ms;
} aster_power_key_t;
typedef struct {
 uint32_t size,version;const char *name;
 int (*caps)(void *context,aster_power_caps_t *out);
 int (*battery)(void *context,aster_power_battery_t *out);
 int (*screen)(void *context,bool on,uint8_t brightness);
 /* suspend returns after wake for light sleep. Deep sleep and shutdown may
  * never return. Return an error if requested wake sources cannot be armed. */
 int (*suspend)(void *context,const aster_power_request_t *request);
 int (*restart)(void *context);
 int (*shutdown)(void *context);
 int (*keys)(void *context,aster_power_key_t *events,uint32_t capacity);
 /* Physical code + hold time -> semantic action. Overridable by each board. */
 aster_power_key_action_t (*map_key)(void *context,uint32_t code,bool pressed,uint32_t held_ms);
} aster_power_ops_t;
/* Lifecycle is a separate override table: OS/app/SD teardown must not be
 * embedded in PMIC drivers. prepare must either obtain exclusive quiescence,
 * or fail without changing state. finish releases it on wake/failure.
 * Screen-off needs neither hook; destructive/CPU-sleep actions require both. */
typedef struct {
 uint32_t size,version;const char *name;
 int (*prepare)(void *context,const aster_power_request_t *request);
 void (*finish)(void *context,const aster_power_request_t *request,int result);
} aster_power_lifecycle_t;
int aster_power_derive(aster_power_ops_t *out,const aster_power_ops_t *base,const aster_power_ops_t *overrides);
int aster_power_lifecycle_derive(aster_power_lifecycle_t *out,const aster_power_lifecycle_t *base,const aster_power_lifecycle_t *overrides);
const aster_power_ops_t *aster_power_ops(void);
/* Portable display/key base for OS and board inheritance. */
extern const aster_power_ops_t aster_power_device_base;
void aster_power_default_init(void);
int aster_power_bind(const aster_power_ops_t *ops,void *context);
int aster_power_bind_lifecycle(const aster_power_lifecycle_t *ops,void *context);
void aster_power_seal(void);
int aster_power_caps(aster_power_caps_t *out);
int aster_power_battery(aster_power_battery_t *out);
int aster_power_keys(aster_power_key_t *events,uint32_t capacity);
aster_power_key_action_t aster_power_map_key(uint32_t code,bool pressed,uint32_t held_ms);
int aster_power_transition(const aster_power_request_t *request,uint8_t brightness);
int aster_power_screen_on(uint8_t brightness);
/* UI-owner idle policy. Zero timeout disables automatic screen-off. Activity
 * wakes the screen, consumes the entire waking gesture until all contacts
 * release, then starts the timeout again. No allocation or timer task. */
typedef struct {
 uint64_t last_activity_ms;uint32_t timeout_ms;
 uint8_t brightness;bool initialized,screen_off,wake_gesture;
} aster_power_idle_t;
void aster_power_idle_init(aster_power_idle_t *state,uint64_t now,uint32_t timeout_ms,uint8_t brightness);
int aster_power_idle_tick(aster_power_idle_t *state,uint64_t now,bool inhibited);
bool aster_power_idle_activity(aster_power_idle_t *state,uint64_t now,bool contacts_down);
#endif

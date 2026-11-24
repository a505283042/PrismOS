/* SPDX-License-Identifier: MIT */
#include "aster_idf_ui.h"
#include "aster_idf_runtime.h"
#include "graphics.h"
#include "aster_platform.h"
#include "aster_device.h"
#include "aster_power.h"
#include "ui_lvgl.h"
#include "ui_font.h"
#include "font_sd.h"
#include "ui_shell.h"
#include "qpanel.h"
#include "wifi_settings.h"
#include "aster_setup.h"
#include "aster_deploy.h"
#include "aster_deploy_ui.h"
#include "gesture.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "flux_draw.h"
#ifndef ASTER_IDF_HOST_TEST
#include "sdkconfig.h"
#include "ppa_fill.h"
#include "render_workers.h"
#if defined(CONFIG_IDF_TARGET_ESP32S31)
#include "stage_c_bench.h"
#include "aster_player_service.h"
/* Optional board-owned profiler. The generic UI component never touches
 * LCD registers or assumes an S31 driver on other IDF targets. */
extern void aster_idf_lcd_profile_begin(void) __attribute__((weak));
extern void aster_idf_lcd_profile_end(bool report) __attribute__((weak));
extern void aster_idf_lcd_pipe_set_bench(bool enabled) __attribute__((weak));
extern void aster_idf_lcd_pipe_set_mode(bool enabled) __attribute__((weak));
extern void aster_idf_lcd_pipe_status(bool *requested, bool *enabled, bool *fault)
    __attribute__((weak));
#endif
#endif
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static lv_display_t *display;
static lv_indev_t *pointer;
static uint32_t pointer_x, pointer_y;
static bool pressed, app_active;
static int (*launch_callback)(const char *);
static atomic_int perf_request;
#if !defined(ASTER_IDF_HOST_TEST) && defined(CONFIG_IDF_TARGET_ESP32S31)
/* Claim at command acceptance (before the UI worker processes it) so no
 * Stage C DMA/CPU test can overlap the launcher rendering benchmark. */
static atomic_bool ui_bench_reserved;
/* Kept on UI worker; restored to PPA after CPU-only measurement. */
static bool ui_bench_cpu_only;
static bool ui_bench_pipe;
/* Console requests only enter an atomic mailbox, handled by the UI/APP owner.
 * S31 defaults to ping-pong on boot; a manual OFF remains until next reboot.
 * No NVS write or persistent setting is needed. */
static atomic_int ui_pipe_request; /* 1 on, 2 off, 3 status */
static bool ui_pipe_runtime;
#endif

static struct {
    uint64_t since, render_start, last_frame, render_us, flush_us, bytes, pump_us;
    uint32_t frames, flushes, max_render_us, max_pump_us, interval[256];
} perf;

static uint64_t bench_warm, bench_end, bench_step;

static void perf_reset(void)
{
    aster_idf_graphics_perf_reset();
    memset(&perf, 0, sizeof(perf));
    perf.since = esp_timer_get_time();
    aster_flux_draw_time_reset();
#ifndef ASTER_IDF_HOST_TEST
    aster_idf_ppa_fill_reset();
    aster_idf_render_workers_reset();
#endif
}

static void perf_print(void)
{
    uint32_t bakes, hits, cached;
    aster_ui_shell_art_stats(&bakes, &hits, &cached);
    uint64_t elapsed = esp_timer_get_time() - perf.since;
    unsigned p95 = 0, total = 0, goal = perf.frames > 1 ? (perf.frames - 1) * 95 / 100 : 0;
    for (unsigned i = 0; i < 256; i++) {
        total += perf.interval[i];
        if (total >= goal) {
            p95 = i;
            break;
        }
    }
    printf(
        "UI PERF frames=%lu elapsed_ms=%llu fps_x10=%llu render_avg_us=%llu render_max_us=%lu "
        "interval_p95_ms=%u flush_us=%llu flushes=%lu bytes=%llu pump_us=%llu pump_max_us=%lu ribbon_us=%lu\n",
        (unsigned long)perf.frames, (unsigned long long)(elapsed / 1000),
        (unsigned long long)(elapsed ? (uint64_t)perf.frames * 10000000 / elapsed : 0),
        (unsigned long long)(perf.frames ? perf.render_us / perf.frames : 0),
        (unsigned long)perf.max_render_us, p95, (unsigned long long)perf.flush_us,
        (unsigned long)perf.flushes, (unsigned long long)perf.bytes,
        (unsigned long long)perf.pump_us, (unsigned long)perf.max_pump_us,
        (unsigned long)aster_flux_draw_time_reset());
    aster_idf_graphics_perf_report();
    printf("UI ART bakes=%lu hits=%lu inactive_bytes=%lu\n", (unsigned long)bakes,
           (unsigned long)hits, (unsigned long)cached);
    aster_launcher_slider_stats_t slider;
    aster_ui_shell_slider_stats(&slider);
    printf(
        "UI SLIDER presses=%lu releases=%lu changes=%lu value=%ld saves=%lu set_max_us=%lu save_max_ms=%lu\n",
        (unsigned long)slider.presses, (unsigned long)slider.releases,
        (unsigned long)slider.changes, (long)slider.value, (unsigned long)slider.saves,
        (unsigned long)slider.max_set_us, (unsigned long)slider.max_save_ms);
#ifndef ASTER_IDF_HOST_TEST
    aster_idf_ppa_fill_report();
    aster_idf_render_workers_report();
#endif
}

static void perf_event(lv_event_t *event)
{
    uint64_t now = esp_timer_get_time();
    if (lv_event_get_code(event) == LV_EVENT_RENDER_START) {
        perf.render_start = now;
        if (perf.last_frame) {
            unsigned ms = (now - perf.last_frame) / 1000;
            perf.interval[ms < 255 ? ms : 255]++;
        }
        perf.last_frame = now;
    } else {
        uint32_t us = (uint32_t)(now - perf.render_start);
        perf.frames++;
        perf.render_us += us;
        if (us > perf.max_render_us)
            perf.max_render_us = us;
    }
}

int aster_idf_ui_command(int argc, char **argv)
{
    if (argc == 3 && !strcmp(argv[2], "setup-status")) {
        aster_setup_snapshot s;
        aster_setup_get(&s);
        printf("SETUP phase=%u error=%d hotspot=%u ssid=%s bytes=%llu/%llu\n",
               (unsigned)s.state.phase, s.state.error, s.hotspot, s.ssid,
               (unsigned long long)s.state.bytes, (unsigned long long)s.state.total);
        return 0;
    }
    if (argc == 3 && !strcmp(argv[2], "setup"))
        return aster_setup_repair();
    if (argc == 4 && !strcmp(argv[2], "setup-source"))
        return aster_setup_repair_source(argv[3]);
#if !defined(ASTER_IDF_HOST_TEST) && defined(CONFIG_IDF_TARGET_ESP32S31)
    /* Stage C never changes the live renderer. Each run requires idle audio
     * and an exited APP; the worker is separate from UI/console tasks. */
    int stage_c_mode = -1;
    if (argc == 3 && !strcmp(argv[2], "stagec"))
        stage_c_mode = 0;
    else if (argc == 4 && !strcmp(argv[2], "stagec-power")) {
        if (!strcmp(argv[3], "cpu")) stage_c_mode = 1;
        else if (!strcmp(argv[3], "dma2d")) stage_c_mode = 2;
        else if (!strcmp(argv[3], "idle")) stage_c_mode = 3;
        else return ASTER_EINVAL;
    }
    if (stage_c_mode >= 0) {
        if (atomic_load(&ui_bench_reserved)) {
            puts("STAGEC refused: UI BENCH still running");
            return ASTER_EBUSY;
        }
        if (app_active) {
            puts("STAGEC refused: exit the APP before benchmarking");
            return ASTER_EBUSY;
        }
        /* The native player snapshot covers *both* local and Bluetooth
         * source playback; testing a local media snapshot alone would miss
         * a background A2DP receiver session. */
        aster_player_info_t player={.size=sizeof(player),.version=1};
        if (!aster_player_service_available() ||
            aster_player_service_info(&player) ||
            player.state==ASTER_PLAYER_OPENING ||
            player.state==ASTER_PLAYER_PLAYING ||
            player.state==ASTER_PLAYER_PAUSED ||
            player.state==ASTER_PLAYER_STOPPING) {
            puts("STAGEC refused: stop local/BT playback first");
            return ASTER_EBUSY;
        }
        if (!aster_idf_ppa_fill_ready()) {
            puts("STAGEC refused: PPA Fill did not pass both boot self-tests");
            return ASTER_ESTATE;
        }
        int started=aster_idf_stage_c_start(stage_c_mode);
        if (started)
            puts("STAGEC refused: worker already running or unavailable");
        return started ? ASTER_EBUSY : 0;
    }
#endif
#if !defined(ASTER_IDF_HOST_TEST) && defined(CONFIG_IDF_TARGET_ESP32S31)
    if (argc == 4 && !strcmp(argv[2], "pipe")) {
        int action = !strcmp(argv[3], "on") ? 1 :
                     !strcmp(argv[3], "off") ? 2 :
                     !strcmp(argv[3], "status") ? 3 : 0;
        if (!action)
            return ASTER_EINVAL;
        if (action != 3 && (atomic_load(&ui_bench_reserved) ||
                            aster_idf_stage_c_running())) {
            puts("UI PIPE refused: a benchmark is running");
            return ASTER_EBUSY;
        }
        int empty = 0;
        if (!atomic_compare_exchange_strong(&ui_pipe_request, &empty, action))
            return ASTER_EBUSY;
        printf("UI PIPE queued action=%s (UI/APP worker applies it)\n", argv[3]);
        return 0;
    }
#endif
    int request;
    if (argc == 4 && !strcmp(argv[2], "volume")) {
        char *end = NULL;
        long value = strtol(argv[3], &end, 10);
        if (*end || value < 0 || value > 100)
            return ASTER_EINVAL;
        request = 1000 + (int)value;
    } else if (argc == 3 && !strcmp(argv[2], "perf"))
        request = 1;
    else if (argc == 3 && !strcmp(argv[2], "power-status"))
        request = 2;
    else if ((argc == 3 || argc == 4) &&
             (!strcmp(argv[2], "bench")
#if !defined(ASTER_IDF_HOST_TEST) && defined(CONFIG_IDF_TARGET_ESP32S31)
              || !strcmp(argv[2], "bench-cpu")
              || !strcmp(argv[2], "bench-ppa")
              || !strcmp(argv[2], "bench-pipe")
#endif
             )) {
        char *end = NULL;
        long seconds = argc == 4 ? strtol(argv[3], &end, 10) : 8;
        if (seconds < 2 || seconds > 20 || (end && *end))
            return ASTER_EINVAL;
        request = 100 + (int)seconds;
#if !defined(ASTER_IDF_HOST_TEST) && defined(CONFIG_IDF_TARGET_ESP32S31)
        if (!strcmp(argv[2], "bench-cpu")) request = 200 + (int)seconds;
        if (!strcmp(argv[2], "bench-ppa")) request = 300 + (int)seconds;
        if (!strcmp(argv[2], "bench-pipe")) request = 400 + (int)seconds;
#endif
    } else
        return ASTER_EINVAL;
#if !defined(ASTER_IDF_HOST_TEST) && defined(CONFIG_IDF_TARGET_ESP32S31)
    if (request > 100 && request < 1000) {
        if (atomic_load(&ui_pipe_request)) {
            puts("UI BENCH refused: LCD runtime mode change pending");
            return ASTER_EBUSY;
        }
        if (aster_idf_stage_c_running()) {
            puts("UI BENCH refused: Stage C CPU/DMA2D worker running");
            return ASTER_EBUSY;
        }
        bool free_slot=false;
        if (!atomic_compare_exchange_strong(&ui_bench_reserved,&free_slot,true)) {
            puts("UI BENCH refused: another UI benchmark running");
            return ASTER_EBUSY;
        }
    }
#endif
    int empty = 0;
    if (atomic_compare_exchange_strong(&perf_request, &empty, request))
        return 0;
#if !defined(ASTER_IDF_HOST_TEST) && defined(CONFIG_IDF_TARGET_ESP32S31)
    if (request > 100 && request < 1000)
        atomic_store(&ui_bench_reserved,false);
#endif
    return ASTER_EBUSY;
}

#if !defined(ASTER_IDF_HOST_TEST) && defined(CONFIG_IDF_TARGET_ESP32S31)
static void ui_pipe_poll(void)
{
    int action = atomic_exchange(&ui_pipe_request, 0);
    if (!action)
        return;
    if (!aster_idf_lcd_pipe_set_mode || !aster_idf_lcd_pipe_status) {
        puts("UI PIPE unavailable: board runtime hooks missing");
        return;
    }
    if (action != 3) {
        /* A benchmark may have been accepted after the command was queued. */
        if (atomic_load(&ui_bench_reserved)) {
            puts("UI PIPE refused: a UI benchmark is running");
            return;
        }
        aster_idf_lcd_pipe_set_mode(action == 1);
    }

    bool requested = false, enabled = false, fault = false;
    aster_idf_lcd_pipe_status(&requested, &enabled, &fault);
    if (action == 1) {
        ui_pipe_runtime = enabled && !fault;
        if (!ui_pipe_runtime && !fault) {
            /* Failed allocation leaves the production mode untouched. */
            aster_idf_lcd_pipe_set_mode(false);
            aster_idf_lcd_pipe_status(&requested, &enabled, &fault);
        }
    } else if (action == 2) {
        ui_pipe_runtime = false;
    }
    printf("UI PIPE runtime=%s requested=%u enabled=%u fault=%u app=%u bench=%u\n",
           ui_pipe_runtime ? "ON" : "OFF",
           requested ? 1u : 0u, enabled ? 1u : 0u, fault ? 1u : 0u,
           app_active ? 1u : 0u,
           atomic_load(&ui_bench_reserved) ? 1u : 0u);
    if (fault)
        puts("UI PIPE fault: LCD submissions blocked; reboot required");
}
#endif

static void perf_tick(void)
{
    uint64_t now = esp_timer_get_time();
    int request = atomic_exchange(&perf_request, 0);
    if (request == 1)
        perf_print();
    else if (request == 2) {
        aster_power_caps_t caps = {0};
        aster_power_battery_t battery = {0};
        int c = aster_power_caps(&caps), b = aster_power_battery(&battery);
        const aster_power_ops_t *ops = aster_power_ops();
        printf(
            "POWER adapter=%s caps_status=%d modes=%lu screen_wake=%lu battery_status=%d flags=%lu percent=%lu keys=%d\n",
            ops ? ops->name : "none", c, (unsigned long)caps.modes, (unsigned long)caps.screen_wake,
            b, (unsigned long)battery.flags, (unsigned long)battery.percent, caps.keys);
    } else if (request >= 1000 && request <= 1100) {
        uint64_t start = esp_timer_get_time();
        int result = aster_device_audio_set_volume(request - 1000);
        printf("UI VOLUME percent=%d result=%d elapsed_us=%llu\n", request - 1000, result,
               (unsigned long long)(esp_timer_get_time() - start));
    } else if (request > 100) {
        if (app_active) {
            puts("UI BENCH busy: stop the application first");
#if !defined(ASTER_IDF_HOST_TEST) && defined(CONFIG_IDF_TARGET_ESP32S31)
            atomic_store(&ui_bench_reserved,false);
#endif
            return;
        }
        int seconds = request - 100;
#if !defined(ASTER_IDF_HOST_TEST) && defined(CONFIG_IDF_TARGET_ESP32S31)
        if (request >= 400) seconds = request - 400;
        else if (request >= 300) seconds = request - 300;
        else if (request >= 200) seconds = request - 200;
        ui_bench_cpu_only = request >= 200 && request < 300;
        ui_bench_pipe = request >= 400 && request < 500;
        aster_idf_ppa_fill_set_bench_cpu_only(ui_bench_cpu_only);
        if (aster_idf_lcd_pipe_set_bench)
            aster_idf_lcd_pipe_set_bench(ui_bench_pipe);
#endif
        aster_ui_shell_show_page(0);
        bench_step = now;
        bench_warm = now + 2000000;
        bench_end = bench_warm + seconds * 1000000ULL;
        printf("UI BENCH warmup=2s measurement=%ds\n", seconds);
#if !defined(ASTER_IDF_HOST_TEST) && defined(CONFIG_IDF_TARGET_ESP32S31)
        printf("UI BENCH FILL mode=%s LCD=%s (diagnostic only)\n",
               ui_bench_cpu_only ? "CPU" : "PPA",
               ui_bench_pipe ? "PIPE_REQUESTED" : "SINGLE");
#endif
    }
    if (!bench_end)
        return;
    if (now >= bench_end) {
        bench_end = 0;
        /* Capture UI elapsed time before printing the extra LCD breakdown.
         * Console output itself is outside both benchmark intervals. */
        perf_print();
#if !defined(ASTER_IDF_HOST_TEST) && defined(CONFIG_IDF_TARGET_ESP32S31)
        if (aster_idf_lcd_profile_end)
            aster_idf_lcd_profile_end(true);
        if (aster_idf_lcd_pipe_set_bench)
            aster_idf_lcd_pipe_set_bench(ui_pipe_runtime);
        ui_bench_pipe = false;
#endif
#if !defined(ASTER_IDF_HOST_TEST) && defined(CONFIG_IDF_TARGET_ESP32S31)
        printf("UI BENCH RESULT fill_mode=%s (production PPA restored)\n",
               ui_bench_cpu_only ? "CPU" : "PPA");
        aster_idf_ppa_fill_set_bench_cpu_only(false);
        ui_bench_cpu_only = false;
#endif
        puts("UI BENCH done");
#if !defined(ASTER_IDF_HOST_TEST) && defined(CONFIG_IDF_TARGET_ESP32S31)
        atomic_store(&ui_bench_reserved,false);
#endif
        return;
    }
    if (bench_warm && now >= bench_warm) {
        bench_warm = 0;
        perf_reset();
#if !defined(ASTER_IDF_HOST_TEST) && defined(CONFIG_IDF_TARGET_ESP32S31)
        if (aster_idf_lcd_profile_begin)
            aster_idf_lcd_profile_begin();
#endif
    }
    if (now >= bench_step) {
        aster_ui_shell_step_page(1);
        bench_step = now + 180000;
    }
}

static uint32_t tick(void)
{
    return esp_timer_get_time() / 1000;
}

static void flush(lv_display_t *d, const lv_area_t *area, uint8_t *pixels)
{
    if (!app_active || aster_qpanel_app_holds_glass()) {
        uint32_t w = area->x2 - area->x1 + 1, h = area->y2 - area->y1 + 1;
        uint64_t start = esp_timer_get_time();
        uint32_t pitch = lv_display_get_horizontal_resolution(d);
        const uint8_t *source = pixels + ((size_t)area->y1 * pitch + area->x1) * 2;
        aster_plat_display_write_at(source, area->x1, area->y1, w, h, pitch);
        perf.flush_us += esp_timer_get_time() - start;
        perf.flushes++;
        perf.bytes += (uint64_t)w * h * 2;
    }
    lv_display_flush_ready(d);
}

static void read_pointer(lv_indev_t *indev, lv_indev_data_t *out)
{
    (void)indev;
    out->point.x = pointer_x;
    out->point.y = pointer_y;
    out->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

int aster_ui_lvgl_init(void)
{
    if (display)
        return 0;
    extern void aster_idf_render_init(void);
    aster_idf_render_init();
    aster_display_info_t info;
    if (aster_plat_display_get_info(0, &info))
        return ASTER_ENODEV;
    /* Keep a fixed full-screen stride while drawing only damaged areas.
     * Cache-aligned rows permit PPA and independent SMP rows even for narrow
     * damage rectangles. Flush remains synchronous with one owned buffer. */
    size_t bytes = ((size_t)info.width * info.height * 2 + 63) & ~(size_t)63;
    void *a = heap_caps_aligned_alloc(64, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!a)
        return ASTER_ENOMEM;
    display = lv_display_create(info.width, info.height);
    if (!display) {
        free(a);
        return ASTER_ENOMEM;
    }
    lv_tick_set_cb(tick);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, a, NULL, bytes, LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(display, flush);
    perf_reset();
    lv_display_add_event_cb(display, perf_event, LV_EVENT_RENDER_START, NULL);
    lv_display_add_event_cb(display, perf_event, LV_EVENT_RENDER_READY, NULL);
#ifndef ASTER_IDF_HOST_TEST
    (void)aster_idf_ppa_fill_init();
#endif
    pointer = lv_indev_create();
    if (!pointer) {
        lv_display_delete(display);
        display = NULL;
        free(a);
        return ASTER_ENOMEM;
    }
    lv_indev_set_type(pointer, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(pointer, read_pointer);
    aster_font_sd_init();
    return 0;
}

void aster_ui_lvgl_pointer(uint32_t x, uint32_t y, int down)
{
    pointer_x = x;
    pointer_y = y;
    pressed = down != 0;
}

void aster_ui_lvgl_pump(void)
{
    if (display)
        lv_timer_handler();
}

void aster_ui_lvgl_request_frame(void)
{
    if (display)
        lv_timer_ready(lv_display_get_refr_timer(display));
}

lv_obj_t *aster_ui_lvgl_screen(void)
{
    return display ? lv_screen_active() : NULL;
}

uint32_t aster_ui_lvgl_width(void)
{
    return display ? lv_display_get_horizontal_resolution(display) : 0;
}

uint32_t aster_ui_lvgl_height(void)
{
    return display ? lv_display_get_vertical_resolution(display) : 0;
}

uint32_t aster_ui_lvgl_layout_width(void)
{
    return aster_ui_lvgl_width();
}

uint32_t aster_ui_lvgl_layout_height(void)
{
    return aster_ui_lvgl_height();
}

void aster_ui_lvgl_resync_window(void)
{
}

lv_indev_t *aster_ui_lvgl_indev(void)
{
    return pointer;
}

int aster_runtime_launch_async(const char *id)
{
    return launch_callback ? launch_callback(id) : ASTER_ESTATE;
}

static bool touch(void *value)
{
    aster_event_t *event = value;
    bool down = event->u.touch.action != ASTER_TOUCH_UP;
    static bool suppress_multi;
    static uint32_t primary = UINT32_MAX;
    uint32_t mask = aster_idf_touch_mask();
    if (aster_launcher_power_touch(mask != 0))
        return true;
    bool open = aster_qpanel_is_open();
    if (mask && (mask & (mask - 1))) {
        suppress_multi = true;
        primary = UINT32_MAX;
        aster_gesture_reset();
        aster_ui_lvgl_pointer(event->u.touch.x, event->u.touch.y, 0);
    }
    if (suppress_multi) {
        if (!mask)
            suppress_multi = false;
        return open;
    }
    if (event->u.touch.action == ASTER_TOUCH_DOWN &&
        (primary == UINT32_MAX || !(mask & (1u << primary))))
        primary = event->u.touch.slot;
    if (primary != event->u.touch.slot)
        return open;
    if (!down)
        primary = UINT32_MAX;
    bool was_open = aster_qpanel_is_open();
    aster_gest_t gesture = aster_gesture_feed(event->u.touch.x, event->u.touch.y, down,
                                              aster_qpanel_allow(false), aster_ui_lvgl_height());
    bool consumed = aster_qpanel_on_gesture(gesture, ASTER_QPANEL_OVER_APP);
    if (gesture == ASTER_GEST_PANEL_UP) {
        aster_qpanel_request_exit("应用");
        consumed = true;
    }
    if (was_open || aster_qpanel_is_open() || consumed) {
        aster_ui_lvgl_pointer(event->u.touch.x, event->u.touch.y, down);
        return true;
    }
    return false;
}

static void app_tick(void)
{
#if !defined(ASTER_IDF_HOST_TEST) && defined(CONFIG_IDF_TARGET_ESP32S31)
    ui_pipe_poll();
#endif
    aster_ui_fonts_advance();
    aster_qpanel_tick();
    if (aster_qpanel_is_open())
        aster_ui_lvgl_pump();
    if (aster_qpanel_take_exit_confirmed())
        aster_idf_exit_current(0);
}

int aster_idf_ui_init(int (*launch)(const char *))
{
    launch_callback = launch;
    int r = aster_ui_lvgl_init();
    if (r)
        return r;
#if !defined(ASTER_IDF_HOST_TEST) && defined(CONFIG_IDF_TARGET_ESP32S31)
    /* The APP worker owns the LCD. Enable the already-tested 16-row pipe
     * BEFORE the first launcher frame, not from the console or an ISR.
     * If internal DMA RAM is unavailable, keep the synchronous baseline.
     * A manual pipe off applies to the current boot only. */
    ui_pipe_runtime = false;
    if (aster_idf_lcd_pipe_set_mode && aster_idf_lcd_pipe_status) {
        aster_idf_lcd_pipe_set_mode(true);
        bool requested = false, enabled = false, fault = false;
        aster_idf_lcd_pipe_status(&requested, &enabled, &fault);
        ui_pipe_runtime = enabled && !fault;
        if (!ui_pipe_runtime && !fault) {
            /* An allocation fallback is not a live pipeline request. */
            aster_idf_lcd_pipe_set_mode(false);
            aster_idf_lcd_pipe_status(&requested, &enabled, &fault);
        }
        printf("UI PIPE boot default=ON runtime=%s requested=%u enabled=%u fault=%u\n",
               ui_pipe_runtime ? "ON" : "OFF",
               requested ? 1u : 0u, enabled ? 1u : 0u, fault ? 1u : 0u);
    } else {
        puts("UI PIPE boot default=ON fallback=single reason=hooks_unavailable");
    }
#endif
    aster_ui_shell_init();
    aster_ui_shell_request_show();
    const aster_idf_ui_hooks_t hooks = {touch, app_tick, aster_qpanel_app_holds_glass};
    aster_idf_set_ui_hooks(&hooks);
    return 0;
}

void aster_idf_ui_idle(void)
{
    if (!display)
        return;
#if !defined(ASTER_IDF_HOST_TEST) && defined(CONFIG_IDF_TARGET_ESP32S31)
    ui_pipe_poll();
#endif
    if (aster_deploy_ui_tick())
        return;
    perf_tick();
    uint64_t start = esp_timer_get_time();
    aster_ui_shell_pump_idle();
    uint32_t us = (uint32_t)(esp_timer_get_time() - start);
    perf.pump_us += us;
    if (us > perf.max_pump_us)
        perf.max_pump_us = us;
}

void aster_idf_ui_before_app(void)
{
    aster_deploy_snapshot s;
    if (aster_deploy_get(&s))
        aster_deploy_dismiss(s.generation);
    aster_deploy_ui_close();
#if !defined(ASTER_IDF_HOST_TEST) && defined(CONFIG_IDF_TARGET_ESP32S31)
    if (aster_idf_lcd_profile_end)
        aster_idf_lcd_profile_end(false);
    /* Only an interrupted benchmark overrides the real runtime selection.
     * Launcher -> APP must preserve the opt-in mode. */
    if (atomic_load(&ui_bench_reserved) && aster_idf_lcd_pipe_set_bench)
        aster_idf_lcd_pipe_set_bench(ui_pipe_runtime);
    ui_bench_pipe = false;
    /* Opening an APP cancels any in-flight or still-pending UI A/B test.
     * Without this, a CPU-only test interrupted by an app launch could
     * permanently leave the launcher PPA accelerator disabled. */
    int pending=atomic_load(&perf_request);
    if (pending>100 && pending<1000)
        (void)atomic_compare_exchange_strong(&perf_request,&pending,0);
    if (atomic_load(&ui_bench_reserved)) {
        puts("UI BENCH aborted: application launched; PPA restored");
        aster_idf_ppa_fill_set_bench_cpu_only(false);
        ui_bench_cpu_only = false;
        atomic_store(&ui_bench_reserved,false);
    }
#endif
    bench_end = bench_warm = 0;
    app_active = true;
    aster_ui_shell_hide();
    aster_qpanel_force_close();
    aster_gesture_reset();
}

void aster_idf_ui_after_app(void)
{
    app_active = false;
    aster_qpanel_on_app_stop();
    aster_ui_shell_request_show();
}

/* Installer runs on this same UI/APP worker after the guest has stopped.
 * Pumping here keeps LVGL/touch alive without adding a task or SRAM stack. */
static lv_obj_t *install_root, *install_stage, *install_size, *install_file, *install_bar,
    *install_clock, *install_detail_text;
static bool install_details;
static uint64_t install_since, install_last_pump, install_last_draw;
static aster_install_phase install_previous;

static lv_obj_t *install_label(lv_obj_t *root, int y, unsigned px)
{
    lv_obj_t *label = lv_label_create(root);
    lv_obj_set_pos(label, 24, y);
    lv_obj_set_width(label, aster_ui_lvgl_width() - 48);
    lv_obj_set_style_text_font(label, aster_ui_font(px), 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xe7edf5), 0);
    return label;
}

static void install_toggle(lv_event_t *event)
{
    (void)event;
    install_details = !install_details;
    if (install_details)
        lv_obj_remove_flag(install_file, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(install_file, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(install_detail_text, install_details ? "收起详情" : "查看详情");
}

void aster_idf_ui_install_progress(void *context, aster_install_phase phase, uint64_t done,
                                   uint64_t total, const char *file)
{
    (void)context;
    if (!display)
        return;
    uint64_t now = esp_timer_get_time();
    if (!install_root) {
        app_active = false;
        aster_qpanel_force_close();
        aster_gesture_reset();
        install_since = now;
        install_last_pump = install_last_draw = 0;
        install_details = false;
        install_previous = ASTER_INSTALL_DONE;
        install_root = lv_obj_create(lv_layer_top());
        lv_obj_set_size(install_root, aster_ui_lvgl_width(), aster_ui_lvgl_height());
        lv_obj_set_pos(install_root, 0, 0);
        lv_obj_remove_flag(install_root, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_pad_all(install_root, 0, 0);
        lv_obj_set_style_border_width(install_root, 0, 0);
        lv_obj_set_style_radius(install_root, 0, 0);
        lv_obj_set_style_bg_color(install_root, lv_color_hex(0x101820), 0);
        lv_obj_set_style_bg_opa(install_root, LV_OPA_COVER, 0);
        lv_label_set_text(install_label(install_root, 48, 24), "正在安装应用");
        install_stage = install_label(install_root, 116, 20);
        install_size = install_label(install_root, 160, 18);
        install_bar = lv_bar_create(install_root);
        lv_obj_set_pos(install_bar, 24, 208);
        lv_obj_set_size(install_bar, aster_ui_lvgl_width() - 48, 18);
        lv_bar_set_range(install_bar, 0, 1000);
        install_clock = install_label(install_root, 250, 18);
        install_file = install_label(install_root, 290, 18);
        lv_obj_add_flag(install_file, LV_OBJ_FLAG_HIDDEN);
        lv_obj_t *button = lv_button_create(install_root);
        lv_obj_set_pos(button, 24, aster_ui_lvgl_height() - 100);
        lv_obj_set_size(button, aster_ui_lvgl_width() - 48, 42);
        lv_obj_add_event_cb(button, install_toggle, LV_EVENT_CLICKED, NULL);
        install_detail_text = lv_label_create(button);
        lv_obj_set_style_text_font(install_detail_text, aster_ui_font(18), 0);
        lv_label_set_text(install_detail_text, "查看详情");
        lv_obj_center(install_detail_text);
        lv_label_set_text(install_label(install_root, aster_ui_lvgl_height() - 44, 18),
                          "安装中，请勿断电");
        aster_ui_font_warm(18,
                           "已复制用时秒查看详情收起安装中请勿断电准备文件资源同步提交版本完成");
    }
    if (now - install_last_pump < 50000 && phase == install_previous)
        return;
    install_last_pump = now;
    aster_touch_evt_t touch_events[16];
    int n;
    for (unsigned batch = 0; batch < 16 && (n = aster_plat_input_poll(touch_events, 16)) > 0;
         batch++)
        for (int i = 0; i < n; i++) {
            aster_touch_evt_t *t = &touch_events[i];
            bool one = !(t->contact_mask & (t->contact_mask - 1));
            aster_ui_lvgl_pointer(t->x, t->y, one && t->action != ASTER_TOUCH_UP);
            lv_indev_read(pointer);
        }
    if (now - install_last_draw >= 200000 || phase != install_previous) {
        const char *names[] = {"正在检查安装包", "正在复制资源", "正在同步文件", "正在提交版本",
                               "安装完成"};
        lv_label_set_text(install_stage, names[phase]);
        char text[128];
        if (total)
            snprintf(text, sizeof(text), "已复制 %llu / %llu KiB  (%u%%)",
                     (unsigned long long)(done / 1024), (unsigned long long)(total / 1024),
                     (unsigned)(done * 100 / total));
        else
            snprintf(text, sizeof(text), "正在准备文件…");
        lv_label_set_text(install_size, text);
        int value =
            total ? (int)(done * 990 / total) : (int)(((now - install_since) / 100000) % 900);
        if (phase == ASTER_INSTALL_COMMIT)
            value = 990;
        if (phase == ASTER_INSTALL_DONE)
            value = 1000;
        lv_bar_set_value(install_bar, value, LV_ANIM_OFF);
        snprintf(text, sizeof(text), "用时 %llu 秒",
                 (unsigned long long)((now - install_since) / 1000000));
        lv_label_set_text(install_clock, text);
        const char *base = strrchr(file, '/');
        snprintf(text, sizeof(text), "%.90s", base ? base + 1 : file);
        lv_label_set_text(install_file, text);
        install_previous = phase;
        install_last_draw = now;
    }
    if (aster_ui_fonts_poll())
        lv_obj_invalidate(install_root);
    aster_ui_lvgl_request_frame();
    aster_ui_lvgl_pump();
}

void aster_idf_ui_install_end(void)
{
    if (!install_root)
        return;
    lv_obj_delete(install_root);
    install_root = NULL;
    aster_ui_lvgl_pointer(0, 0, 0);
    aster_gesture_reset();
}

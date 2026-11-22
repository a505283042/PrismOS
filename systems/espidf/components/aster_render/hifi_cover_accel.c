/* SPDX-License-Identifier: MIT
 * ESP32-S31 cover JPEG hardware path. Never DMA from/to WASM linear memory.
 * JPEG buffer / MCU padding / PPA buffers are owned by this native service;
 * only completed 460x460-or-smaller RGB565 images reach the app.
 *
 * The legacy TJpgDec path remains in HiFiPlayer for unsupported, progressive,
 * oversized, corrupt, or unavailable hardware decode cases.
 */
#include "sdkconfig.h"
#if defined(CONFIG_IDF_TARGET_ESP32S31)

#include "hifi_cover_accel.h"
#include "imgdec_internal.h"
#include "aster_storage.h"

#include "driver/jpeg_decode.h"
#include "driver/ppa.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <zephyr/fs/fs.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* A full 1400px square cover is 3.9MB of JPEG output. Refuse larger
 * operations instead of stealing all 16MB PSRAM from audio and WASM. */
enum { MAX_JPEG_COMPRESSED = 2 * 1024 * 1024, MAX_JPEG_PIXELS = 2100000,
       ALIGNMENT = 64, DIM_ALPHA = 151 };
static const char *TAG = "hifi_cover_hw";
static SemaphoreHandle_t decoder_lock;
static jpeg_decoder_handle_t engine;
static bool engine_failed;
static ppa_client_handle_t blend_engine;
static bool blend_failed;
static bool logged_success;

static uint32_t align_up(uint32_t n, uint32_t unit)
{
    return (n + unit - 1u) / unit * unit;
}

static void *psram_aligned(size_t size)
{
    size_t rounded = (size + ALIGNMENT - 1u) & ~(size_t)(ALIGNMENT - 1u);
    return heap_caps_aligned_alloc(ALIGNMENT, rounded, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

static bool init_jpeg(void)
{
    if (engine)
        return true;
    if (engine_failed)
        return false;
    jpeg_decode_engine_cfg_t cfg = {.intr_priority = 0, .timeout_ms = 800};
    esp_err_t r = jpeg_new_decoder_engine(&cfg, &engine);
    if (r != ESP_OK || !engine) {
        engine = NULL;
        engine_failed = true;
        ESP_LOGW(TAG, "JPEG engine unavailable: %s", esp_err_to_name(r));
    }
    return engine != NULL;
}

static bool dim_using_ppa(const uint16_t *raw, uint16_t *dim,
                          uint32_t pitch, uint32_t height)
{
    if (blend_failed)
        return false;
    if (!blend_engine) {
        ppa_client_config_t config = {
            .oper_type = PPA_OPERATION_BLEND,
            .max_pending_trans_num = 1
        };
        esp_err_t r = ppa_register_client(&config, &blend_engine);
        if (r != ESP_OK || !blend_engine) {
            blend_engine = NULL;
            blend_failed = true;
            ESP_LOGW(TAG, "PPA Blend unavailable: %s", esp_err_to_name(r));
            return false;
        }
    }
    size_t count = (size_t)pitch * height;
    uint8_t *mask = psram_aligned(count);
    if (!mask)
        return false;
    memset(mask, DIM_ALPHA, count);
    ppa_blend_oper_config_t config = {0};
    config.in_bg.buffer = raw;
    config.in_bg.pic_w = pitch;
    config.in_bg.pic_h = height;
    config.in_bg.block_w = pitch;
    config.in_bg.block_h = height;
    config.in_bg.blend_cm = PPA_BLEND_COLOR_MODE_RGB565;
    config.in_fg.buffer = mask;
    config.in_fg.pic_w = pitch;
    config.in_fg.pic_h = height;
    config.in_fg.block_w = pitch;
    config.in_fg.block_h = height;
    config.in_fg.blend_cm = PPA_BLEND_COLOR_MODE_A8;
    config.fg_fix_rgb_val.val = 0;
    config.out.buffer = dim;
    config.out.buffer_size = count * sizeof(uint16_t);
    config.out.pic_w = pitch;
    config.out.pic_h = height;
    config.out.blend_cm = PPA_BLEND_COLOR_MODE_RGB565;
    config.mode = PPA_TRANS_MODE_BLOCKING;
    esp_err_t r = ppa_do_blend(blend_engine, &config);
    heap_caps_free(mask);
    if (r != ESP_OK) {
        ESP_LOGW(TAG, "PPA Blend failed (%s); software dim fallback",
                 esp_err_to_name(r));
        return false;
    }
    return true;
}

static void dim_using_cpu(const uint16_t *raw, uint16_t *dim, size_t count)
{
    /* Match existing APP brightness (105/255) within RGB565 rounding. */
    for (size_t i = 0; i < count; i++) {
        uint16_t c = raw[i];
        dim[i] = (uint16_t)((((c >> 11) * 105u / 255u) << 11) |
                            ((((c >> 5) & 63u) * 105u / 255u) << 5) |
                            ((c & 31u) * 105u / 255u));
    }
}

static uint16_t sample_rgb565(const uint16_t *src, uint32_t pitch,
                              uint32_t x0, uint32_t x1, uint32_t y0,
                              uint32_t y1, uint32_t wx, uint32_t wy)
{
    uint16_t a = src[(size_t)y0 * pitch + x0];
    uint16_t b = src[(size_t)y0 * pitch + x1];
    uint16_t c = src[(size_t)y1 * pitch + x0];
    uint16_t d = src[(size_t)y1 * pitch + x1];
    uint32_t w00 = (256u - wx) * (256u - wy);
    uint32_t w10 = wx * (256u - wy);
    uint32_t w01 = (256u - wx) * wy;
    uint32_t w11 = wx * wy;
    uint32_t red = ((((a >> 11) * w00 + (b >> 11) * w10 +
                      (c >> 11) * w01 + (d >> 11) * w11) + 32768u) >> 16);
    uint32_t green = (((((a >> 5) & 63u) * w00 + ((b >> 5) & 63u) * w10 +
                        ((c >> 5) & 63u) * w01 + ((d >> 5) & 63u) * w11)
                       + 32768u) >> 16);
    uint32_t blue = (((((a & 31u) * w00 + (b & 31u) * w10 +
                       (c & 31u) * w01 + (d & 31u) * w11) + 32768u) >> 16));
    if (red > 31) red = 31;
    if (green > 63) green = 63;
    if (blue > 31) blue = 31;
    return (uint16_t)((red << 11) | (green << 5) | blue);
}

/* Center-of-pixel bilinear downsample, no crop: same fit-to-cap behaviour as
 * imgdec.c. A rectangular source remains rectangular for cover_paint crop. */
static void shrink_image(const uint16_t *src, uint32_t sw, uint32_t sh,
                         uint32_t spitch, uint16_t *dst, uint32_t dpitch,
                         uint32_t dw, uint32_t dh, bool nearest)
{
    for (uint32_t y = 0; y < dh; y++) {
        uint32_t yf = ((uint64_t)(2u * y + 1u) * sh * 256u / (2u * dh));
        yf = yf > 128u ? yf - 128u : 0;
        uint32_t y0 = yf >> 8;
        if (y0 >= sh) y0 = sh - 1u;
        uint32_t y1 = y0 + 1u < sh ? y0 + 1u : y0;
        uint32_t wy = nearest ? 0 : (yf & 255u);
        for (uint32_t x = 0; x < dw; x++) {
            uint32_t xf = ((uint64_t)(2u * x + 1u) * sw * 256u / (2u * dw));
            xf = xf > 128u ? xf - 128u : 0;
            uint32_t x0 = xf >> 8;
            if (x0 >= sw) x0 = sw - 1u;
            uint32_t x1 = x0 + 1u < sw ? x0 + 1u : x0;
            uint32_t wx = nearest ? 0 : (xf & 255u);
            dst[(size_t)y * dpitch + x] = nearest ?
                src[(size_t)y0 * spitch + x0] :
                sample_rgb565(src, spitch, x0, x1, y0, y1, wx, wy);
        }
    }
}

int aster_hifi_cover_render(const char *path, uint32_t src_offset, uint32_t src_length,
                            uint16_t *guest_original, uint16_t *guest_dim,
                            uint32_t cap_w, uint32_t cap_h, int nearest,
                            uint32_t *out_w, uint32_t *out_h, uint32_t *result_flags)
{
    if (!path || !guest_original || !guest_dim || !cap_w || !cap_h ||
        cap_w > 480 || cap_h > 480 || !out_w || !out_h || !result_flags)
        return ASTER_EINVAL;
    if (!decoder_lock)
        decoder_lock = xSemaphoreCreateMutex();
    if (!decoder_lock)
        return ASTER_ENOMEM;
    xSemaphoreTake(decoder_lock, portMAX_DELAY);
    int result = ASTER_ENOTSUP;
    uint8_t *compressed = NULL;
    uint8_t *decoded = NULL;
    uint16_t *scaled = NULL;
    uint16_t *dark = NULL;
    struct fs_file_t file;
    bool opened = false;
    fs_file_t_init(&file);
    do {
        struct fs_dirent entry;
        if (fs_stat(path, &entry) || entry.type != FS_DIR_ENTRY_FILE) {
            result = ASTER_ENOENT;
            break;
        }
        uint64_t size = (uint64_t)entry.size;
        if (!src_length && !src_offset) {
            if (size > UINT32_MAX) { result = ASTER_ENOTSUP; break; }
            src_length = (uint32_t)size;
        }
        if (!src_length || src_length > MAX_JPEG_COMPRESSED ||
            (uint64_t)src_offset + src_length > size) {
            result = ASTER_ENOTSUP;
            break;
        }
        if (fs_open(&file, path, FS_O_READ) != 0) {
            result = ASTER_ENOENT;
            break;
        }
        opened = true;
        if (fs_seek(&file, (off_t)src_offset, FS_SEEK_SET)) {
            result = ASTER_EIO;
            break;
        }
        compressed = psram_aligned(src_length);
        if (!compressed) { result = ASTER_ENOMEM; break; }
        size_t got = 0;
        while (got < src_length) {
            /* A single 1-2MB FatFS read would block the audio decoder on
             * the shared TF card. Read 8KB bursts and yield between bursts. */
            size_t burst = src_length - got;
            if (burst > 8192u) burst = 8192u;
            int n = aster_fs_read_safe(&file, compressed + got, burst);
            if (n <= 0) break;
            got += (size_t)n;
            if (got < src_length)
                vTaskDelay(1);
        }
        /* Do not retain an SD handle while JPEG/PPA use shared memory. */
        (void)fs_close(&file);
        opened = false;
        if (got != src_length) { result = ASTER_EIO; break; }
        jpeg_decode_picture_info_t info = {0};
        if (jpeg_decoder_get_info(compressed, src_length, &info) != ESP_OK ||
            !info.width || !info.height || (uint64_t)info.width * info.height > MAX_JPEG_PIXELS ||
            ((uint64_t)info.width * info.height) % 8u) {
            result = ASTER_ENOTSUP;
            break;
        }
        if (!init_jpeg()) { result = ASTER_ENOTSUP; break; }
        uint32_t mcux = 8, mcuy = 8;
        if (info.sample_method == JPEG_DOWN_SAMPLING_YUV420) {
            mcux = mcuy = 16;
        } else if (info.sample_method == JPEG_DOWN_SAMPLING_YUV422) {
            mcux = 16;
        }
        uint32_t pw = align_up(info.width, mcux), ph = align_up(info.height, mcuy);
        size_t decoded_bytes = (size_t)pw * ph * 2u;
        jpeg_decode_memory_alloc_cfg_t mem = {.buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER};
        size_t capacity = 0;
        decoded = jpeg_alloc_decoder_mem(decoded_bytes, &mem, &capacity);
        if (!decoded || capacity < decoded_bytes) { result = ASTER_ENOMEM; break; }
        /* The S31 JPEG peripheral uses RGB/BGR selection as its 16-bit
         * byte-element order. BGR produces the native little-endian RGB565
         * expected by LVGL, Aster's surface ABI, and PPA. RGB is the
         * big-endian/wire-order variant and visibly swaps red and blue
         * when read through uint16_t on RISC-V. Keep this consistent with
         * FakePod_S31 jpeg_hw_decode_rgb565(..., JPEG_HW_ORDER_LE). */
        jpeg_decode_cfg_t cfg = {
            .output_format = JPEG_DECODE_OUT_FORMAT_RGB565,
            .rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_BGR,
            .conv_std = JPEG_YUV_RGB_CONV_STD_BT601
        };
        uint32_t actual = 0;
        int64_t start = esp_timer_get_time();
        esp_err_t e = jpeg_decoder_process(engine, &cfg, compressed, src_length,
                                           decoded, (uint32_t)capacity, &actual);
        if (e != ESP_OK || actual != decoded_bytes) {
            ESP_LOGW(TAG, "JPEG hardware fallback: %s, bytes=%lu/%lu",
                     esp_err_to_name(e), (unsigned long)actual,
                     (unsigned long)decoded_bytes);
            result = ASTER_ENOTSUP;
            break;
        }
        uint32_t w = 0, h = 0;
        double scale;
        img_fit_size(info.width, info.height, cap_w, cap_h, &w, &h, &scale);
        if (!w || !h) { result = ASTER_EINVAL; break; }
        uint32_t pitch = align_up(w, 16u), height = align_up(h, 16u);
        size_t layer_bytes = (size_t)pitch * height * sizeof(uint16_t);
        scaled = psram_aligned(layer_bytes);
        dark = psram_aligned(layer_bytes);
        if (!scaled || !dark) { result = ASTER_ENOMEM; break; }
        memset(scaled, 0, layer_bytes);
        memset(dark, 0, layer_bytes);
        shrink_image((const uint16_t *)decoded, info.width, info.height, pw,
                     scaled, pitch, w, h, nearest != 0);
        bool accelerated_dim = dim_using_ppa(scaled, dark, pitch, height);
        if (!accelerated_dim)
            dim_using_cpu(scaled, dark, (size_t)pitch * height);
        /* Publish both layers only after all work succeeded. They are linear
         * WASM copies, NEVER hardware DMA destinations. No transient cover. */
        for (uint32_t y = 0; y < h; y++) {
            memcpy(guest_original + (size_t)y * w, scaled + (size_t)y * pitch,
                   (size_t)w * sizeof(uint16_t));
            memcpy(guest_dim + (size_t)y * w, dark + (size_t)y * pitch,
                   (size_t)w * sizeof(uint16_t));
        }
        *out_w = w;
        *out_h = h;
        *result_flags = ASTER_HIFI_COVER_HW_JPEG |
                        (accelerated_dim ? ASTER_HIFI_COVER_PPA_DIM : ASTER_HIFI_COVER_CPU_DIM);
        if (!logged_success) {
            logged_success = true;
            ESP_LOGI(TAG, "JPEG %lux%lu -> %lux%lu hw=%lldus PPA=%s",
                     (unsigned long)info.width, (unsigned long)info.height,
                     (unsigned long)w, (unsigned long)h,
                     (long long)(esp_timer_get_time() - start),
                     accelerated_dim ? "yes" : "fallback");
        }
        result = ASTER_OK;
    } while (0);
    if (opened)
        (void)fs_close(&file);
    heap_caps_free(compressed);
    heap_caps_free(decoded);
    heap_caps_free(scaled);
    heap_caps_free(dark);
    xSemaphoreGive(decoder_lock);
    return result;
}
#endif /* CONFIG_IDF_TARGET_ESP32S31 */

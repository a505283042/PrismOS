/* SPDX-License-Identifier: MIT */
#include <wasm_export.h>
#include <aster/aster.h>
#include "aster_idf_runtime.h"
#include "esp_timer.h"
#include <string.h>
/* Native board callbacks provide measurements; the ABI owns validation,
 * coherent units and the same 10 ms sample cache used by the Zephyr port. */
static aster_imu_sample_t sample;
static int64_t sampled_at = -1;
static int sampled_result = ASTER_ENODEV;

static void *output(wasm_exec_env_t env, uint32_t off, uint32_t bytes)
{
    wasm_module_inst_t instance = wasm_runtime_get_module_inst(env);
    if (!off || !wasm_runtime_validate_app_addr(instance, off, bytes))
        return NULL;
    return wasm_runtime_addr_app_to_native(instance, off);
}

static int fetch(void)
{
    int64_t now = esp_timer_get_time() / 1000;
    if (sampled_at >= 0 && now - sampled_at < 10)
        return sampled_result;
    aster_imu_sample_t next = {.size = sizeof(next),
                               .version = 1,
                               .sequence = sample.sequence + 1,
                               .flags = ASTER_IMU_ACCEL_VALID | ASTER_IMU_GYRO_VALID};
    sampled_result = aster_idf_imu_read(&next.accel[0], &next.accel[1], &next.accel[2],
                                        &next.gyro[0], &next.gyro[1], &next.gyro[2]);
    sampled_at = esp_timer_get_time() / 1000;
    if (!sampled_result) {
        next.timestamp_ms = sampled_at;
        sample = next;
    }
    return sampled_result;
}

static int32_t imu_info(wasm_exec_env_t env, uint32_t off, uint32_t size)
{
    void *dst;
    if (size < sizeof(aster_imu_info_t) || !(dst = output(env, off, sizeof(aster_imu_info_t))))
        return ASTER_EINVAL;
    int r = fetch();
    if (r)
        return r;
    aster_imu_info_t info = {.size = sizeof(info),
                             .version = 1,
                             .flags = ASTER_IMU_ACCEL_VALID | ASTER_IMU_GYRO_VALID,
                             .min_interval_ms = 10};
    memcpy(dst, &info, sizeof(info));
    return 0;
}

static int32_t imu_read(wasm_exec_env_t env, uint32_t off, uint32_t size)
{
    void *dst;
    if (size < sizeof(sample) || !(dst = output(env, off, sizeof(sample))))
        return ASTER_EINVAL;
    int r = fetch();
    if (!r)
        memcpy(dst, &sample, sizeof(sample));
    return r;
}

static int32_t vector(wasm_exec_env_t env, uint32_t off, uint32_t size, bool gyro)
{
    void *dst;
    if (size < 3 * sizeof(float) || !(dst = output(env, off, 3 * sizeof(float))))
        return ASTER_EINVAL;
    int r = fetch();
    if (!r)
        memcpy(dst, gyro ? sample.gyro : sample.accel, 3 * sizeof(float));
    return r;
}

static int32_t accel(wasm_exec_env_t e, uint32_t o, uint32_t s)
{
    return vector(e, o, s, false);
}

static int32_t gyro(wasm_exec_env_t e, uint32_t o, uint32_t s)
{
    return vector(e, o, s, true);
}

static int32_t absent(wasm_exec_env_t env, uint32_t out, uint32_t size)
{
    (void)env;
    (void)out;
    (void)size;
    return ASTER_ENODEV;
}

static int32_t keys(wasm_exec_env_t env, uint32_t out, uint32_t max)
{
    (void)env;
    (void)out;
    (void)max;
    return 0;
}

static NativeSymbol symbols[] = {
    {"aster_accel_read", (void *)accel, "(ii)i", NULL},
    {"aster_gyro_read", (void *)gyro, "(ii)i", NULL},
    {"aster_imu_get_info", (void *)imu_info, "(ii)i", NULL},
    {"aster_imu_read", (void *)imu_read, "(ii)i", NULL},
    {"aster_cam_info", (void *)absent, "(ii)i", NULL},
    {"aster_cam_read", (void *)absent, "(ii)i", NULL},
    {"aster_input_keys_state", (void *)keys, "(ii)i", NULL},
};

NativeSymbol *aster_idf_device_symbols(uint32_t *count)
{
    *count = sizeof(symbols) / sizeof(symbols[0]);
    return symbols;
}

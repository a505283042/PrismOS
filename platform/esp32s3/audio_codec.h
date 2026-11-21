/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdint.h>

/* Link exactly one codec per board. Calls run under the platform audio lock.
 * init/enable receive the current UI volume (0..100); errors are negative errno.
 * set_rate may be NULL when clocks are entirely managed by the I2S port.
 * identify must accept either output as NULL and write zero for unknown IDs. */
typedef struct {
    int (*init)(uint8_t volume);
    int (*enable)(uint8_t volume);
    int (*set_volume)(uint8_t volume);
    int (*set_rate)(uint32_t hz);
    void (*identify)(int init_error, uint8_t *dac_id, uint8_t *adc_id);
} aster_audio_codec_ops_t;

extern const aster_audio_codec_ops_t aster_audio_codec;

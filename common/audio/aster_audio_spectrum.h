/* SPDX-License-Identifier: MIT */
#ifndef ASTER_AUDIO_SPECTRUM_H
#define ASTER_AUDIO_SPECTRUM_H

#include <stdatomic.h>
#include <stdint.h>

typedef struct {
    int32_t low_pass[2][3], coefficients[3];
    uint32_t energy[2][4], pending, window;
    atomic_uint levels[2];
} aster_audio_spectrum_t;

void aster_audio_spectrum_reset(aster_audio_spectrum_t *spectrum, uint32_t rate);
void aster_audio_spectrum_add(aster_audio_spectrum_t *spectrum, const void *pcm,
                              uint32_t frames, unsigned channels, unsigned sample_bytes);

#endif

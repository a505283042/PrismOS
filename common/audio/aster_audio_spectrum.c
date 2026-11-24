/* SPDX-License-Identifier: MIT */
#include "aster_audio_spectrum.h"
#include <string.h>

void aster_audio_spectrum_reset(aster_audio_spectrum_t *s, uint32_t rate)
{
    /* Approximate crossover points at 180 Hz, 750 Hz and 3.3 kHz. */
    static const uint32_t radians_per_second[3] = {1131, 4712, 20735};
    for (unsigned band = 0; band < 3; band++) {
        uint32_t corner = radians_per_second[band];
        s->coefficients[band] = corner * 32768 / (rate + corner);
    }
    memset(s->low_pass, 0, sizeof(s->low_pass));
    memset(s->energy, 0, sizeof(s->energy));
    s->pending = 0;
    s->window = rate / 25;
    atomic_store(&s->levels[0], 0);
    atomic_store(&s->levels[1], 0);
}

static void publish(aster_audio_spectrum_t *s)
{
    for (unsigned channel = 0; channel < 2; channel++) {
        unsigned packed = 0;
        for (unsigned band = 0; band < 4; band++) {
            unsigned level = s->energy[channel][band] / s->window;
            unsigned scale = (level * 255) / (level + 1600);
            packed |= scale << (band * 8);
            s->energy[channel][band] = 0;
        }
        atomic_store(&s->levels[channel], packed);
    }
    s->pending = 0;
}

void aster_audio_spectrum_add(aster_audio_spectrum_t *s, const void *pcm,
                              uint32_t frames, unsigned channels, unsigned sample_bytes)
{
    /* The caller supplies accepted little-endian PCM frames. Keep the most
     * significant 16 bits so all supported source bit depths use one scale. */
    const uint8_t *sample = pcm;
    for (uint32_t frame = 0; frame < frames; frame++, sample += channels*sample_bytes) {
        for (unsigned channel = 0; channel < 2; channel++) {
            const uint8_t *p = sample + (channels == 1 ? 0 : channel*sample_bytes) + sample_bytes-2;
            int32_t input = (int32_t)p[0] + ((int32_t)p[1] - (p[1] >= 128 ? 256 : 0))*256;
            int32_t previous = 0;
            for (unsigned band = 0; band < 4; band++) {
                int32_t level = input;
                if (band < 3) {
                    int32_t *low = &s->low_pass[channel][band];
                    *low += (input-*low)*s->coefficients[band]/32768;
                    level = *low;
                }
                int32_t difference = level-previous;
                s->energy[channel][band] += (uint32_t)(difference < 0 ? -difference : difference);
                previous = level;
            }
        }
        if (++s->pending == s->window) publish(s);
    }
}

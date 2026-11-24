/* SPDX-License-Identifier: MIT */
#ifndef ASTER_AUDIO_OUTPUT_H
#define ASTER_AUDIO_OUTPUT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* Firmware-internal optional audio output contract. Not a WASM ABI header.
 * Bind/derive at boot; bound ops/context must outlive the registry.
 * The media worker serializes endpoint operations.
 * Return values are native negative errno, mapped at the public ABI boundary. */
#define ASTER_AUDIO_OUTPUT_API_VERSION 1u
#define ASTER_AUDIO_OUTPUT_WIRED 1u
#define ASTER_AUDIO_OUTPUT_BLUETOOTH 2u
#define ASTER_AUDIO_OUTPUT_COUNT 3u
#define ASTER_AUDIO_PCM_S16LE 1u
#define ASTER_AUDIO_PCM_S24LE 2u
#define ASTER_AUDIO_PCM_S32LE 3u
#define ASTER_AUDIO_PCM_BIT(f) (1u << (f))
#define ASTER_AUDIO_RATE_32000 (1u << 0)
#define ASTER_AUDIO_RATE_44100 (1u << 1)
#define ASTER_AUDIO_RATE_48000 (1u << 2)
#define ASTER_AUDIO_RATE_88200 (1u << 3)
#define ASTER_AUDIO_RATE_96000 (1u << 4)
#define ASTER_AUDIO_CHANNEL_BIT(n) (1u << (n))
#define ASTER_AUDIO_OUTPUT_EXCLUSIVE 1u

typedef struct {
    uint32_t size, version, rate, channels, format, flags;
} aster_audio_format_t;

typedef struct {
    uint32_t size, version, formats, rates, channels, flags;
    uint32_t max_write_frames;
} aster_audio_output_caps_t;

typedef struct {
    uint32_t size, version, rate, channels, format, flags;
    uint64_t submitted_frames;
    uint32_t underruns, errors;
} aster_audio_output_stats_t;

typedef struct {
    uint32_t size, version;
    const char *name;
    int (*caps)(void *context, aster_audio_output_caps_t *out);
    int (*open)(void *context, const aster_audio_format_t *format);
    /* Complete interleaved frames accepted, partial count, or negative errno.
  * Return only after consuming/copying caller bytes; never retain pointers. */
    int (*write)(void *context, const void *pcm, uint32_t frames);
    int (*drain)(void *context);
    int (*close)(void *context);
    int (*stats)(void *context, aster_audio_output_stats_t *out);
} aster_audio_output_ops_t;

uint32_t aster_audio_rate_bit(uint32_t rate);
size_t aster_audio_frame_bytes(const aster_audio_format_t *format);
/* Input/output buffers must not overlap. Samples are left aligned in s32. */
int aster_audio_pcm_to_s32(const void *pcm, uint32_t frames, const aster_audio_format_t *format,
                           int32_t *out, size_t out_samples);
int aster_audio_output_derive(aster_audio_output_ops_t *out, const aster_audio_output_ops_t *base,
                              const aster_audio_output_ops_t *overrides);
int aster_audio_output_bind(uint32_t endpoint, const aster_audio_output_ops_t *ops, void *context);
void aster_audio_output_seal(void);
int aster_audio_output_caps(uint32_t endpoint, aster_audio_output_caps_t *out);
int aster_audio_output_open(uint32_t endpoint, const aster_audio_format_t *format);
int aster_audio_output_write(uint32_t endpoint, const void *pcm, uint32_t frames);
int aster_audio_output_drain(uint32_t endpoint);
int aster_audio_output_close(uint32_t endpoint);
int aster_audio_output_stats(uint32_t endpoint, aster_audio_output_stats_t *out);
#if ASTER_AUDIO_SPECTRUM_ENABLED
/* Four 8-bit low-to-high bands per channel, least significant band first. */
void aster_audio_output_spectrum(uint32_t endpoint, uint32_t bands[2]);
#endif
#endif

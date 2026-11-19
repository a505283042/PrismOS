/* SPDX-License-Identifier: MIT */
#include "aster_audio_output.h"
#include "aster_power.h"
#include <errno.h>
#include <limits.h>
#include <string.h>
#if ASTER_AUDIO_SPECTRUM_ENABLED
#include "aster_audio_spectrum.h"
static aster_audio_spectrum_t spectrum[ASTER_AUDIO_OUTPUT_COUNT];
#endif
static struct binding {
    const aster_audio_output_ops_t *ops;
    void *context;
    bool opened;
    aster_audio_format_t format;
} outputs[ASTER_AUDIO_OUTPUT_COUNT];

static bool sealed;

static bool endpoint_valid(uint32_t id)
{
    return id > 0 && id < ASTER_AUDIO_OUTPUT_COUNT;
}

static bool ops_valid(const aster_audio_output_ops_t *ops)
{
    return ops && ops->size == sizeof(*ops) && ops->version == ASTER_AUDIO_OUTPUT_API_VERSION &&
           ops->name && ops->caps && ops->open && ops->write && ops->close;
}

uint32_t aster_audio_rate_bit(uint32_t rate)
{
    switch (rate) {
    case 32000:
        return ASTER_AUDIO_RATE_32000;
    case 44100:
        return ASTER_AUDIO_RATE_44100;
    case 48000:
        return ASTER_AUDIO_RATE_48000;
    case 88200:
        return ASTER_AUDIO_RATE_88200;
    case 96000:
        return ASTER_AUDIO_RATE_96000;
    default:
        return 0;
    }
}

size_t aster_audio_frame_bytes(const aster_audio_format_t *f)
{
    if (!f || f->size != sizeof(*f) || f->version != 1 || !aster_audio_rate_bit(f->rate) ||
        !f->channels || f->channels > 2 || (f->flags & ~ASTER_AUDIO_OUTPUT_EXCLUSIVE))
        return 0;
    unsigned bytes = f->format == ASTER_AUDIO_PCM_S16LE   ? 2
                     : f->format == ASTER_AUDIO_PCM_S24LE ? 3
                     : f->format == ASTER_AUDIO_PCM_S32LE ? 4
                                                          : 0;
    return bytes * f->channels;
}

int aster_audio_pcm_to_s32(const void *pcm, uint32_t frames, const aster_audio_format_t *f,
                           int32_t *out, size_t capacity)
{
    size_t frame = aster_audio_frame_bytes(f);
    if (!frame || !pcm || !out || !frames || frames > SIZE_MAX / frame ||
        frames > capacity / f->channels)
        return -EINVAL;
    unsigned bytes = (unsigned)(frame / f->channels);
    size_t n = (size_t)frames * f->channels;
    const uint8_t *p = pcm;
    /* Decode little-endian bytes without unaligned loads or shifting a negative
  * signed value. The 24-bit path preserves every bit including the LSB. */
    for (size_t i = 0; i < n; i++, p += bytes) {
        uint32_t v;
        if (bytes == 2)
            v = ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 24);
        else if (bytes == 3)
            v = ((uint32_t)p[0] << 8) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 24);
        else
            v = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
                ((uint32_t)p[3] << 24);
        memcpy(out + i, &v, sizeof(v));
    }
    return 0;
}

int aster_audio_output_derive(aster_audio_output_ops_t *out, const aster_audio_output_ops_t *base,
                              const aster_audio_output_ops_t *overrides)
{
    if (!out || !ops_valid(base) || !overrides || overrides->size != sizeof(*overrides) ||
        overrides->version != ASTER_AUDIO_OUTPUT_API_VERSION)
        return -EINVAL;
    aster_audio_output_ops_t merged = *base;
#define OVERRIDE(name) \
    if (overrides->name) \
    merged.name = overrides->name
    OVERRIDE(name);
    OVERRIDE(caps);
    OVERRIDE(open);
    OVERRIDE(write);
    OVERRIDE(drain);
    OVERRIDE(close);
    OVERRIDE(stats);
#undef OVERRIDE
    *out = merged;
    return 0;
}

int aster_audio_output_bind(uint32_t id, const aster_audio_output_ops_t *ops, void *context)
{
    if (sealed)
        return -EBUSY;
    if (!endpoint_valid(id) || !ops_valid(ops))
        return -EINVAL;
    if (outputs[id].ops)
        return -EEXIST;
    outputs[id].ops = ops;
    outputs[id].context = context;
    return 0;
}

void aster_audio_output_seal(void)
{
    sealed = true;
}

int aster_audio_output_caps(uint32_t id, aster_audio_output_caps_t *out)
{
    if (!endpoint_valid(id) || !out)
        return -EINVAL;
    if (!outputs[id].ops)
        return -ENOTSUP;
    aster_audio_output_caps_t next = {.size = sizeof(next), .version = 1};
    int r = outputs[id].ops->caps(outputs[id].context, &next);
    if (r)
        return r;
    const uint32_t formats = ASTER_AUDIO_PCM_BIT(ASTER_AUDIO_PCM_S16LE) |
                             ASTER_AUDIO_PCM_BIT(ASTER_AUDIO_PCM_S24LE) |
                             ASTER_AUDIO_PCM_BIT(ASTER_AUDIO_PCM_S32LE);
    if (next.size != sizeof(next) || next.version != 1 || !next.formats ||
        (next.formats & ~formats) || !next.rates || (next.rates & ~31u) || !next.channels ||
        (next.channels & ~6u) || !next.max_write_frames || next.max_write_frames > INT_MAX ||
        (next.flags & ~ASTER_AUDIO_OUTPUT_EXCLUSIVE))
        return -EIO;
    *out = next;
    return 0;
}

int aster_audio_output_open(uint32_t id, const aster_audio_format_t *f)
{
    if (!endpoint_valid(id) || !aster_audio_frame_bytes(f))
        return -EINVAL;
    if (!outputs[id].ops)
        return -ENOTSUP;
    if (outputs[id].opened)
        return -EBUSY;
    aster_audio_output_caps_t caps;
    int r = aster_audio_output_caps(id, &caps);
    if (r)
        return r;
    if (!(caps.formats & ASTER_AUDIO_PCM_BIT(f->format)) ||
        !(caps.rates & aster_audio_rate_bit(f->rate)) ||
        !(caps.channels & ASTER_AUDIO_CHANNEL_BIT(f->channels)) || (f->flags & ~caps.flags))
        return -ENOTSUP;
    r = aster_power_activity_begin();
    if (r)
        return r;
    r = outputs[id].ops->open(outputs[id].context, f);
    if (!r) {
        outputs[id].opened = true;
        outputs[id].format = *f;
#if ASTER_AUDIO_SPECTRUM_ENABLED
        aster_audio_spectrum_reset(&spectrum[id], f->rate);
#endif
    } else
        aster_power_activity_end();
    return r;
}

int aster_audio_output_write(uint32_t id, const void *pcm, uint32_t frames)
{
    if (!endpoint_valid(id) || !pcm || !frames || frames > INT_MAX)
        return -EINVAL;
    struct binding *b = &outputs[id];
    if (!b->ops)
        return -ENOTSUP;
    if (!b->opened)
        return -EBADF;
    size_t frame_bytes = aster_audio_frame_bytes(&b->format);
    if (frames > SIZE_MAX / frame_bytes)
        return -EINVAL;
    aster_audio_output_caps_t caps;
    int r = aster_audio_output_caps(id, &caps);
    if (r)
        return r;
    if (frames > caps.max_write_frames)
        return -EMSGSIZE;
    r = b->ops->write(b->context, pcm, frames);
#if ASTER_AUDIO_SPECTRUM_ENABLED
    if (r > 0 && (uint32_t)r <= frames)
        aster_audio_spectrum_add(&spectrum[id], pcm, r, b->format.channels,
                                 frame_bytes / b->format.channels);
#endif
    return r > (int)frames ? -EIO : r == 0 ? -EAGAIN : r;
}

int aster_audio_output_drain(uint32_t id)
{
    if (!endpoint_valid(id))
        return -EINVAL;
    struct binding *b = &outputs[id];
    if (!b->ops)
        return -ENOTSUP;
    if (!b->opened)
        return -EBADF;
    return b->ops->drain ? b->ops->drain(b->context) : -ENOTSUP;
}

int aster_audio_output_close(uint32_t id)
{
    if (!endpoint_valid(id))
        return -EINVAL;
    struct binding *b = &outputs[id];
    if (!b->ops)
        return -ENOTSUP;
    if (!b->opened)
        return 0;
    int r = b->ops->close(b->context);
    if (!r) {
#if ASTER_AUDIO_SPECTRUM_ENABLED
        aster_audio_spectrum_reset(&spectrum[id], b->format.rate);
#endif
        b->opened = false;
        memset(&b->format, 0, sizeof(b->format));
        aster_power_activity_end();
    }
    return r;
}

int aster_audio_output_stats(uint32_t id, aster_audio_output_stats_t *out)
{
    if (!endpoint_valid(id) || !out)
        return -EINVAL;
    struct binding *b = &outputs[id];
    if (!b->ops)
        return -ENOTSUP;
    if (!b->ops->stats)
        return -ENOTSUP;
    aster_audio_output_stats_t next = {.size = sizeof(next), .version = 1};
    int r = b->ops->stats(b->context, &next);
    if (!r) {
        if (next.size != sizeof(next) || next.version != 1)
            return -EIO;
        *out = next;
    }
    return r;
}
#if ASTER_AUDIO_SPECTRUM_ENABLED
void aster_audio_output_spectrum(uint32_t id, uint32_t bands[2])
{
    bands[0] = endpoint_valid(id) ? atomic_load(&spectrum[id].levels[0]) : 0;
    bands[1] = endpoint_valid(id) ? atomic_load(&spectrum[id].levels[1]) : 0;
}
#endif

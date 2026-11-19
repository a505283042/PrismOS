/* SPDX-License-Identifier: MIT */
#include "aster_audio_decoder.h"
#include "aster_audio_output.h"
#include "aster_os.h"
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <stddef.h>
#define DR_FLAC_NO_STDIO
#define DR_FLAC_NO_OGG
#define DR_FLAC_NO_SIMD
#define DR_FLAC_IMPLEMENTATION
#include "dr_flac.h"
#define DR_WAV_NO_STDIO
#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"
#define DR_MP3_NO_STDIO
#define DR_MP3_NO_SIMD
#define DR_MP3_ONLY_MP3
#define DR_MP3_IMPLEMENTATION
#include "dr_mp3.h"
#define DECODER_BUDGET (768u * 1024u)

struct aster_audio_decoder {
    aster_os_file_t file;
    uint64_t length, cursor, decoded;
    int io_error;
    bool (*cancel)(void *);
    void *context;
    aster_audio_decoder_info_t info;
    drwav *wav;
    drflac *flac;
    drmp3 *mp3;

    /* Newlib's int32_t can be long while dr_libs uses int. Do not alias the
  * caller's int32_t buffer through an incompatible int pointer. */
    union {
        drflac_int32 flac[1024];
        drwav_int32 wav[1024];
        drmp3_int16 mp3[1024];
    } samples;
};

typedef union {
    max_align_t alignment;
    size_t bytes;
} allocation_header;

static void *decoder_alloc(size_t bytes, void *ctx)
{
    aster_audio_decoder_t *d = ctx;
    if (bytes > DECODER_BUDGET - d->info.allocated ||
        bytes > SIZE_MAX - sizeof(allocation_header)) {
        d->io_error = -ENOMEM;
        return NULL;
    }
    allocation_header *h = aster_os_alloc_external(bytes + sizeof(*h));
    if (!h) {
        d->io_error = -ENOMEM;
        return NULL;
    }
    h->bytes = bytes;
    d->info.allocated += bytes;
    if (d->info.allocated > d->info.peak_allocated)
        d->info.peak_allocated = d->info.allocated;
    return h + 1;
}

static void decoder_free(void *p, void *ctx)
{
    if (!p)
        return;
    aster_audio_decoder_t *d = ctx;
    allocation_header *h = (allocation_header *)p - 1;
    d->info.allocated -= h->bytes;
    aster_os_free_external(h);
}

static void *decoder_realloc(void *p, size_t bytes, void *ctx)
{
    if (!p)
        return decoder_alloc(bytes, ctx);
    if (!bytes) {
        decoder_free(p, ctx);
        return NULL;
    }
    allocation_header *h = (allocation_header *)p - 1;
    void *next = decoder_alloc(bytes, ctx);
    if (!next)
        return NULL;
    memcpy(next, p, bytes < h->bytes ? bytes : h->bytes);
    decoder_free(p, ctx);
    return next;
}

static bool cancelled(aster_audio_decoder_t *d)
{
    if (d->cancel && d->cancel(d->context)) {
        d->io_error = -ECANCELED;
        return true;
    }
    return false;
}

static size_t read_file(void *ctx, void *buffer, size_t bytes)
{
    aster_audio_decoder_t *d = ctx;
    if (d->io_error || cancelled(d))
        return 0;
    if (bytes > d->length - d->cursor)
        bytes = (size_t)(d->length - d->cursor);
    if (bytes > INT_MAX)
        bytes = INT_MAX;
    size_t done = 0;
    while (done < bytes) {
        if (cancelled(d))
            break;
        int n = aster_os_file_read(&d->file, (uint8_t *)buffer + done, bytes - done);
        if (n < 0) {
            d->io_error = n;
            break;
        }
        if (!n) {
            d->io_error = -EIO;
            break;
        }
        if ((size_t)n > bytes - done) {
            d->io_error = -EIO;
            break;
        }
        done += (size_t)n;
        d->cursor += (unsigned)n;
    }
    return done;
}

static int seek_file(aster_audio_decoder_t *d, int offset, unsigned origin)
{
    if (cancelled(d))
        return 0;
    if (origin > 2)
        return 0;
    uint64_t base = origin == 0 ? 0 : origin == 1 ? d->cursor : d->length;
    /* FLAC binary seeking probes beyond EOF while refining an estimate. This
  * is an unsuccessful probe, not a sticky I/O failure; it may recover by
  * trying an earlier offset. Actual filesystem errors remain terminal. */
    uint64_t at;
    if (offset < 0) {
        uint64_t distance = (uint64_t)(-(int64_t)offset);
        if (distance > base)
            return 0;
        at = base - distance;
    } else {
        if ((uint64_t)offset > d->length - base)
            return 0;
        at = base + (unsigned)offset;
    }
    int r = aster_os_file_seek(&d->file, (int64_t)at, ASTER_OS_SEEK_SET);
    if (r) {
        d->io_error = r;
        return 0;
    }
    d->cursor = at;
    return 1;
}

static drwav_bool32 wav_seek(void *ctx, int offset, drwav_seek_origin origin)
{
    return seek_file(ctx, offset, (unsigned)origin);
}

static drflac_bool32 flac_seek(void *ctx, int offset, drflac_seek_origin origin)
{
    return seek_file(ctx, offset, (unsigned)origin);
}

static drwav_bool32 wav_tell(void *ctx, drwav_int64 *cursor)
{
    aster_audio_decoder_t *d = ctx;
    *cursor = (drwav_int64)d->cursor;
    return 1;
}

static drflac_bool32 flac_tell(void *ctx, drflac_int64 *cursor)
{
    aster_audio_decoder_t *d = ctx;
    *cursor = (drflac_int64)d->cursor;
    return 1;
}

static drmp3_bool32 mp3_seek(void *ctx, int offset, drmp3_seek_origin origin)
{
    return seek_file(ctx, offset, (unsigned)origin);
}

static drmp3_bool32 mp3_tell(void *ctx, drmp3_int64 *cursor)
{
    aster_audio_decoder_t *d = ctx;
    *cursor = (drmp3_int64)d->cursor;
    return 1;
}

static bool mp3_path(const char *path)
{
    size_t length = strlen(path);
    if (length < 4)
        return false;
    const char *ext = path + length - 4;
    return ext[0] == '.' && (ext[1] == 'm' || ext[1] == 'M') && (ext[2] == 'p' || ext[2] == 'P') &&
           ext[3] == '3';
}

static int open_mp3(aster_audio_decoder_t *d)
{
    drmp3_allocation_callbacks alloc = {d, decoder_alloc, decoder_realloc, decoder_free};
    drmp3 *mp3 = decoder_alloc(sizeof(*mp3), d);
    if (!mp3)
        return -ENOMEM;
    if (!drmp3_init(mp3, read_file, mp3_seek, mp3_tell, NULL, d, &alloc)) {
        decoder_free(mp3, d);
        return d->io_error ? d->io_error : -EINVAL;
    }
    d->mp3 = mp3;
    d->info.rate = mp3->sampleRate;
    d->info.channels = mp3->channels;
    d->info.bits = 16;
    d->info.codec = ASTER_AUDIO_CODEC_MP3;
    if (!aster_audio_rate_bit(d->info.rate) || !d->info.channels || d->info.channels > 2)
        return -ENOTSUP;
    d->info.frames = drmp3_get_pcm_frame_count(mp3);
    if (d->io_error)
        return d->io_error;
    return d->info.frames ? 0 : -EINVAL;
}

int aster_audio_decoder_close(aster_audio_decoder_t *d)
{
    if (!d)
        return 0;
    if (d->flac)
        drflac_close(d->flac);
    if (d->wav) {
        drwav_uninit(d->wav);
        decoder_free(d->wav, d);
    }
    if (d->mp3) {
        drmp3_uninit(d->mp3);
        decoder_free(d->mp3, d);
    }
    int r = d->file.handle ? aster_os_file_close(&d->file) : 0;
    aster_os_free_external(d);
    return r;
}

int aster_audio_decoder_open(const char *path, bool (*cancel)(void *), void *ctx,
                             aster_audio_decoder_t **out)
{
    if (!path || !out)
        return -EINVAL;
    aster_os_entry_t entry;
    int r = aster_os_stat(path, &entry);
    if (r)
        return r;
    if (entry.type != ASTER_OS_FILE || entry.size < 12 || entry.size > INT64_MAX)
        return -EINVAL;
    aster_audio_decoder_t *d = aster_os_alloc_external(sizeof(*d));
    if (!d)
        return -ENOMEM;
    memset(d, 0, sizeof(*d));
    d->length = entry.size;
    d->cancel = cancel;
    d->context = ctx;
    r = aster_os_file_open(&d->file, path, ASTER_OS_READ);
    if (r) {
        aster_audio_decoder_close(d);
        return r;
    }
    uint8_t magic[12];
    if (read_file(d, magic, sizeof(magic)) != sizeof(magic)) {
        r = d->io_error ? d->io_error : -EIO;
        goto fail;
    }
    if (!seek_file(d, 0, 0)) {
        r = d->io_error;
        goto fail;
    }
    if (!memcmp(magic, "fLaC", 4)) {
        drflac_allocation_callbacks alloc = {d, decoder_alloc, decoder_realloc, decoder_free};
        d->flac = drflac_open(read_file, flac_seek, flac_tell, d, &alloc);
        if (!d->flac) {
            r = d->io_error ? d->io_error : -EINVAL;
            goto fail;
        }
        d->info.rate = d->flac->sampleRate;
        d->info.channels = d->flac->channels;
        d->info.bits = d->flac->bitsPerSample;
        d->info.frames = d->flac->totalPCMFrameCount;
        d->info.codec = ASTER_AUDIO_CODEC_FLAC;
    } else if (!memcmp(magic, "RIFF", 4) && !memcmp(magic + 8, "WAVE", 4)) {
        uint64_t riff = (uint32_t)magic[4] | ((uint32_t)magic[5] << 8) |
                        ((uint32_t)magic[6] << 16) | ((uint32_t)magic[7] << 24);
        if (riff + 8 > d->length) {
            r = -EIO;
            goto fail;
        }
        drwav_allocation_callbacks alloc = {d, decoder_alloc, decoder_realloc, decoder_free};
        drwav *wav = decoder_alloc(sizeof(*wav), d);
        if (!wav) {
            r = -ENOMEM;
            goto fail;
        }
        if (!drwav_init(wav, read_file, wav_seek, wav_tell, d, &alloc)) {
            decoder_free(wav, d);
            r = d->io_error ? d->io_error : -EINVAL;
            goto fail;
        }
        d->wav = wav;
        if (wav->translatedFormatTag != DR_WAVE_FORMAT_PCM) {
            r = -ENOTSUP;
            goto fail;
        }
        d->info.rate = wav->sampleRate;
        d->info.channels = wav->channels;
        d->info.bits = wav->bitsPerSample;
        d->info.frames = wav->totalPCMFrameCount;
        d->info.codec = ASTER_AUDIO_CODEC_WAV;
        if (wav->fmt.formatTag == DR_WAVE_FORMAT_EXTENSIBLE && wav->fmt.validBitsPerSample)
            d->info.bits = wav->fmt.validBitsPerSample;
        if (wav->dataChunkDataPos > d->length ||
            wav->dataChunkDataSize > d->length - wav->dataChunkDataPos ||
            wav->fmt.blockAlign != wav->channels * (wav->bitsPerSample / 8) ||
            !wav->fmt.blockAlign || wav->dataChunkDataSize % wav->fmt.blockAlign) {
            r = -EIO;
            goto fail;
        }
    } else if (mp3_path(path)) {
        r = open_mp3(d);
        if (r)
            goto fail;
    } else {
        r = -ENOTSUP;
        goto fail;
    }
    if (d->io_error) {
        r = d->io_error;
        goto fail;
    }
    if (!aster_audio_rate_bit(d->info.rate) || !d->info.channels || d->info.channels > 2 ||
        (d->info.bits != 16 && d->info.bits != 24 && d->info.bits != 32)) {
        r = -ENOTSUP;
        goto fail;
    }
    *out = d;
    return 0;
fail:
    aster_audio_decoder_close(d);
    return r;
}

void aster_audio_decoder_info(aster_audio_decoder_t *d, aster_audio_decoder_info_t *out)
{
    if (d && out)
        *out = d->info;
}

int aster_audio_decoder_read(aster_audio_decoder_t *d, int32_t *pcm, uint32_t frames)
{
    if (!d || !pcm || !frames || frames > 512)
        return -EINVAL;
    if (cancelled(d))
        return -ECANCELED;
    if (d->io_error)
        return d->io_error;
    uint64_t n;
    if (d->mp3)
        n = drmp3_read_pcm_frames_s16(d->mp3, frames, d->samples.mp3);
    else if (d->flac)
        n = drflac_read_pcm_frames_s32(d->flac, frames, d->samples.flac);
    else
        n = drwav_read_pcm_frames_s32(d->wav, frames, d->samples.wav);
    if (d->io_error)
        return d->io_error;
    for (size_t i = 0; i < (size_t)n * d->info.channels; i++) {
        if (d->mp3)
            pcm[i] = (int32_t)d->samples.mp3[i] * 65536;
        else if (d->flac)
            pcm[i] = d->samples.flac[i];
        else
            pcm[i] = d->samples.wav[i];
    }
    d->decoded += n;
    if (!n && d->info.frames && d->decoded < d->info.frames)
        return -EIO;
    return (int)n;
}

int aster_audio_decoder_seek(aster_audio_decoder_t *d, uint64_t frame)
{
    if (!d || (d->info.frames && frame > d->info.frames))
        return -EINVAL;
    if (cancelled(d))
        return -ECANCELED;
    d->io_error = 0;
    bool ok = d->mp3    ? drmp3_seek_to_pcm_frame(d->mp3, frame)
              : d->flac ? drflac_seek_to_pcm_frame(d->flac, frame)
                        : drwav_seek_to_pcm_frame(d->wav, frame);
    if (d->io_error)
        return d->io_error;
    if (!ok)
        return -EIO;
    d->decoded = frame;
    return 0;
}

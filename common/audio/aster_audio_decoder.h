/* SPDX-License-Identifier: MIT */
#ifndef ASTER_AUDIO_DECODER_H
#define ASTER_AUDIO_DECODER_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
/* Internal, task-owned decoder. Uses Aster OS I/O and external allocations.
 * Output is lossless, left-aligned s32, interleaved source channels.
 * PCM16/24/32 WAV, native FLAC and MP3; no implicit resampling. */
typedef struct aster_audio_decoder aster_audio_decoder_t;
typedef struct {
 uint32_t rate,channels,bits,codec;
 uint64_t frames;
 size_t allocated,peak_allocated;
} aster_audio_decoder_info_t;
#define ASTER_AUDIO_CODEC_WAV 1u
#define ASTER_AUDIO_CODEC_FLAC 2u
#define ASTER_AUDIO_CODEC_MP3 3u
int aster_audio_decoder_open(const char *path,bool (*cancel)(void *),void *context,aster_audio_decoder_t **out);
void aster_audio_decoder_info(aster_audio_decoder_t *decoder,aster_audio_decoder_info_t *out);
int aster_audio_decoder_read(aster_audio_decoder_t *decoder,int32_t *pcm,uint32_t frames);
int aster_audio_decoder_seek(aster_audio_decoder_t *decoder,uint64_t frame);
int aster_audio_decoder_close(aster_audio_decoder_t *decoder);
#endif

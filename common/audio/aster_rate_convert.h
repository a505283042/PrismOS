/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdint.h>
#include <stddef.h>
/* Streaming stereo S32 (left-aligned) -> S16. Caller owns this bounded state.
 * 32-tap/64-phase windowed-sinc low-pass for rate changes. Equal rates bypass
 * the filter. Bluetooth conversion is explicitly lossy, unlike wired output. */
typedef struct {
 uint32_t input_rate,output_rate,phase,index;
 int16_t history[32][2],coefficients[64][32];
} aster_rate_convert_t;
int aster_rate_convert_init(aster_rate_convert_t *state,uint32_t input,uint32_t output);
size_t aster_rate_convert_bound(const aster_rate_convert_t *state,size_t frames);
int aster_rate_convert_process(aster_rate_convert_t *state,const int32_t *input,size_t frames,
                              int16_t *output,size_t capacity_frames);

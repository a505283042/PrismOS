/* SPDX-License-Identifier: MIT
 * Optional S31 hardware JPEG + PPA cover acceleration.
 * Guest pointers are never passed to DMA: the implementation uses its own
 * cache-aligned PSRAM buffers and copies only complete images back to WASM.
 */
#pragma once
#include <stdint.h>
#include <aster/error.h>

/* result_flags: JPEG hardware used; PPA blend used; CPU dim fallback used. */
#define ASTER_HIFI_COVER_HW_JPEG   1u
#define ASTER_HIFI_COVER_PPA_DIM   2u
#define ASTER_HIFI_COVER_CPU_DIM   4u

#if defined(CONFIG_IDF_TARGET_ESP32S31)
int aster_hifi_cover_render(const char *path, uint32_t src_offset, uint32_t src_length,
                            uint16_t *guest_original, uint16_t *guest_dim,
                            uint32_t cap_w, uint32_t cap_h, int nearest,
                            uint32_t *out_w, uint32_t *out_h, uint32_t *result_flags);
#else
static inline int aster_hifi_cover_render(const char *path, uint32_t src_offset,
                                           uint32_t src_length, uint16_t *original,
                                           uint16_t *dim, uint32_t cw, uint32_t ch,
                                           int nearest, uint32_t *w, uint32_t *h,
                                           uint32_t *flags)
{
    (void)path; (void)src_offset; (void)src_length;
    (void)original; (void)dim; (void)cw; (void)ch;
    (void)nearest; (void)w; (void)h; (void)flags;
    return ASTER_ENOTSUP;
}
#endif

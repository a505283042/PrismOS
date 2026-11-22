/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdint.h>
#include <stdbool.h>
bool aster_idf_ppa_fill_rgb565(uint16_t *pixels,uint32_t pitch,uint32_t height,
                             uint32_t x,uint32_t y,uint32_t w,uint32_t h,uint16_t color);
/* True only after both LVGL and large APP-fill RGB565/cache self-tests. */
bool aster_idf_ppa_fill_ready(void);
int aster_idf_ppa_fill_init(void);
void aster_idf_ppa_fill_reset(void);
void aster_idf_ppa_fill_report(void);

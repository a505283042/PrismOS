/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdint.h>
#include "esp_err.h"
esp_err_t fakepod_lcd_init(void);
int fakepod_lcd_present(const uint8_t *pixels,uint32_t x,uint32_t y,uint32_t w,uint32_t h,uint32_t pitch);

#include <stdbool.h>
const uint8_t *fakepod_lcd_capture(void);
int fakepod_lcd_brightness(bool on,uint8_t level);

/* Stage D: board-owned, optional UI benchmark counters.
 * Do not call from ISR, audio task, or production rendering. */
void aster_idf_lcd_profile_begin(void);
void aster_idf_lcd_profile_end(bool report);

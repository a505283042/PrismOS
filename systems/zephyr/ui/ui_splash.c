#include "aster_services.h"
#include "aster_port_util.h"
#include "aster_os.h"
#include "aster_device.h"
/* SPDX-License-Identifier: MIT */
/* Font-free boot image, painted directly as a small RGB565 band. */
#include <string.h>
#include "ui_splash.h"
#include "aster_platform.h"

/* The panel starts at its DT size (320x240 on the reference board), then
 * boot switches it to portrait (240x320). The splash is painted after that
 * switch, so compile-time DT dimensions cannot describe its window. */
#define SPL_H    96
#define SPL_PERIOD 125u	/* repaint at ~8 fps: enough motion, cheap SPI */

#define SPL_MIN_VISIBLE_MS 650u

static const uint8_t wordmark[][7] = {
	{ 14, 17, 17, 31, 17, 17, 17 }, /* A */
	{ 15, 16, 16, 14, 1, 1, 30 },   /* S */
	{ 31, 4, 4, 4, 4, 4, 4 },       /* T */
	{ 31, 16, 16, 30, 16, 16, 31 }, /* E */
	{ 30, 17, 17, 30, 20, 18, 17 }, /* R */
};

static uint16_t *s_band;		/* PSRAM, W*H, native RGB565 */
static uint32_t s_w, s_y0;
static bool s_on;
static uint32_t s_next_ms, s_visible_since;
static uint32_t s_phase;

static void px(int x, int y, uint16_t c)
{
	if ((unsigned)x < s_w && (unsigned)y < SPL_H) {
		s_band[(size_t)y * s_w + x] = c;
	}
}

static void blit(const uint8_t *rows, int nr, int bitsof, int scale,
		 int x0, int y0, uint16_t c)
{
	for (int r = 0; r < nr; r++) {
		for (int b = bitsof - 1; b >= 0; b--) {
			if (!(rows[r] & (1 << b))) {
				continue;
			}
			for (int sy = 0; sy < scale; sy++) {
				for (int sx = 0; sx < scale; sx++) {
					px(x0 + (bitsof - 1 - b) * scale + sx,
					   y0 + r * scale + sy, c);
				}
			}
		}
	}
}

static void paint(void)
{
	const int scale = 4, spacing = 8;
	const int letters = sizeof(wordmark) / sizeof(wordmark[0]);
	const int word_width = letters * 5 * scale + (letters - 1) * spacing;
	int left = ((int)s_w - word_width) / 2;

	memset(s_band, 0, (size_t)s_w * SPL_H * sizeof(*s_band));
	for (int i = 0; i < letters; i++)
		blit(wordmark[i], 7, 5, scale, left + i * (5 * scale + spacing), 18, 0xffff);

	for (int i = 0; i < 3; i++) {
		int cx = (int)s_w / 2 + (i - 1) * 14;
		uint16_t shade = (unsigned)i == (s_phase - 1) % 3 ? 0xdff1 : 0x39e7;
		for (int y = -3; y <= 3; y++)
			for (int x = -3; x <= 3; x++)
				if (x * x + y * y <= 9)
					px(cx + x, 69 + y, shade);
	}

	int err = aster_device_display_write_at((const uint8_t *)s_band, 0, s_y0,
					     s_w, SPL_H, s_w);

	if (err) {
		aster_service_logf(1,"splash write %ux%u at y=%u failed: %d",
			(unsigned)s_w, SPL_H, (unsigned)s_y0, err);
	} else if (s_phase == 1u) {
		aster_service_logf(3,"splash first frame %ux%u at y=%u",
			(unsigned)s_w, SPL_H, (unsigned)s_y0);
	}
}

void aster_splash_boot(void)
{
	aster_display_info_t di = { .size = sizeof(di), .version = 1 };

	if (s_on) {
		return;
	}
	if (aster_device_display_get_info(0, &di) != ASTER_OK ||
	    di.width < 160u || di.height < SPL_H) {
		aster_service_logf(1,"splash: invalid display window %ux%u",
			(unsigned)di.width, (unsigned)di.height);
		return;
	}
	s_w = di.width;
	s_y0 = (di.height - SPL_H) / 2u;
	s_band = aster_os_alloc_external((size_t)s_w * SPL_H * sizeof(*s_band));
	if (!s_band) {
		aster_service_logf(1,"splash: band allocation failed");
		return;
	}
	s_on = true;
	s_phase = 0;
	s_visible_since = aster_os_uptime32();
	s_next_ms = s_visible_since;
	aster_splash_tick();
}

void aster_splash_tick(void)
{
	uint32_t now = (uint32_t)aster_os_uptime32();

	if (!s_on || (int32_t)(now - s_next_ms) < 0) {
		return;
	}
	s_next_ms = now + SPL_PERIOD;
	s_phase++;
	paint();
	(void)aster_device_display_flush_drain();
}

bool aster_splash_minimum_pending(void)
{
	return s_on && (uint32_t)(aster_os_uptime32() - s_visible_since) < SPL_MIN_VISIBLE_MS;
}

void aster_splash_off(void)
{
	if (!s_on) {
		return;
	}
	aster_os_free_external(s_band);
	s_band = NULL;
	s_w = s_y0 = 0;
	s_on = false;
}

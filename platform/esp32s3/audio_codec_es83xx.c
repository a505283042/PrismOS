/* SPDX-License-Identifier: MIT */
/* ES8311 DAC with its own ADC or a separate ES7210. */
#include "audio_codec.h"
#ifdef ASTER_SYSTEM_ESP_IDF
#include "audio_port.h"
#include "es83xx_control.h"
#else
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#endif
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <errno.h>

#ifndef ASTER_SYSTEM_ESP_IDF
BUILD_ASSERT(DT_HAS_CHOSEN(aster_audio_i2c), "select aster,audio-i2c for the ES83xx codec");
#endif

LOG_MODULE_REGISTER(aster_es83xx, LOG_LEVEL_INF);

#define DAC_ADDR 0x18
#define ADC_ADDR 0x41

static const struct device *i2c_dev;
static uint8_t adc_addr = ADC_ADDR;

static int codec_wr(uint8_t addr, uint8_t reg, uint8_t val)
{
	return i2c_reg_write_byte(i2c_dev, addr, reg, val);
}

static int codec_rd(uint8_t addr, uint8_t reg, uint8_t *val)
{
	return i2c_reg_read_byte(i2c_dev, addr, reg, val);
}

static uint8_t vol_to_reg(uint8_t percent)
{
	/* REG32 is linear in dB: 0x00 = -95.5 dB, 0xBF = 0 dB, 0xFF = +32 dB.
	 * Preserve the old mapping through 80 (80 = -8 dB), then extend the top
	 * of the 0..100 ABI scale to +6 dB. Hot mastered tracks can distort at
	 * the high end; no software gain is applied to the PCM samples. */
	int db10;
	int reg;

	if (percent == 0) {
		return 0;
	}
	db10 = percent <= 80 ? -400 + (int)percent * 4 : -80 + ((int)percent - 80) * 7;
	reg = (db10 * 2) / 10 + 191;
	if (reg < 1) {
		reg = 1;
	}
	if (reg > 0xCB) {
		reg = 0xCB;
	}
	return (uint8_t)reg;
}

static int es8311_apply_vol(uint8_t volume)
{
	int err = codec_wr(DAC_ADDR, 0x32, vol_to_reg(volume));
	if (err) {
		return err;
	}
	return codec_wr(DAC_ADDR, 0x31, volume == 0 ? 0x60 : 0x00);
}

/* Official 05-audio_es8311 open + start, clocks for 16 kHz @ MCLK=256*fs. */
static int es8311_dac_on(uint8_t volume)
{
	static const uint8_t tbl[][2] = {
	        {0x00, 0x80}, /* slave, analog on, MCLK from pin */
	        {0x01, 0x3F}, /* clocks on, MCLK pin */
	        {0x09, 0x0C}, /* DAC SDP 16-bit I2S, port enabled */
	        {0x0A, 0x0C},
	        {0x0D, 0x01},
	        {0x0E, 0x02},
	        {0x12, 0x00}, /* DAC analog on */
	        {0x14, 0x1A},
	        {0x15, 0x40},
	        {0x17, IS_ENABLED(CONFIG_ASTER_AUDIO_ADC_ES8311) ? 0xC8 : 0xBF},
	        {0x37, 0x08},
	        {0x45, 0x00},
	};
	int err = 0;

	for (size_t i = 0; i < ARRAY_SIZE(tbl); i++) {
		err = codec_wr(DAC_ADDR, tbl[i][0], tbl[i][1]);
		if (err) {
			return err;
		}
	}
	return es8311_apply_vol(volume);
}

static int es8311_init(uint8_t volume)
{
	uint8_t id = 0;
	int err = codec_rd(DAC_ADDR, 0xFD, &id);

	if (err) {
		LOG_ERR("es8311 probe %d", err);
		return err;
	}
	if (id != 0x83) {
		LOG_WRN("es8311 id 0x%02x (expected 0x83)", id);
	}

	(void)codec_wr(DAC_ADDR, 0x00, 0x1F);
	k_msleep(10);
	(void)codec_wr(DAC_ADDR, 0x00, 0x00);
	(void)codec_wr(DAC_ADDR, 0x44, 0x08);
	(void)codec_wr(DAC_ADDR, 0x44, 0x08);

	static const uint8_t tbl[][2] = {
	        {0x01, 0x30},
	        {0x02, 0x00},
	        {0x03, 0x10},
	        {0x04, 0x10},
	        {0x05, 0x00},
	        {0x0B, 0x00},
	        {0x0C, 0x00},
	        {0x10, 0x1F}, /* analog / VMID up (0x03 left the DAC cold) */
	        {0x11, 0x7F},
	        {0x00, 0x80},
	        {0x01, 0x3F},
	        /* 16 kHz, MCLK = 256 * fs = 4.096 MHz (Zephyr i2s_esp32) */
	        {0x02, 0x00},
	        {0x05, 0x00},
	        {0x03, 0x10},
	        {0x04, 0x20},
	        {0x07, 0x00},
	        {0x08, 0xFF},
	        {0x06, 0x03},
	        {0x13, 0x10},
	        {0x16, 0x24},
	        {0x1B, 0x0A},
	        {0x1C, 0x6A},
	        {0x44, 0x08},
	};
	for (size_t i = 0; i < ARRAY_SIZE(tbl); i++) {
		err = codec_wr(DAC_ADDR, tbl[i][0], tbl[i][1]);
		if (err) {
			return err;
		}
	}
	err = es8311_dac_on(volume);
	if (err) {
		return err;
	}
	LOG_INF("es8311 ready id=0x%02x", id);
	return 0;
}

static int es7210_init(void)
{
	uint8_t id = 0;
	uint8_t try_addr[2] = {ADC_ADDR, 0x40};
	int err = -ENODEV;

	for (int a = 0; a < 2; a++) {
		err = codec_rd(try_addr[a], 0x3D, &id);
		if (err == 0) {
			adc_addr = try_addr[a];
			break;
		}
	}
	if (err) {
		LOG_ERR("es7210 probe %d", err);
		return err;
	}
	if (id != 0x72) {
		LOG_WRN("es7210 id 0x%02x (expected 0x72) @0x%02x", id, adc_addr);
	}

	(void)codec_wr(adc_addr, 0x00, 0xFF);
	k_msleep(10);

	/*
	 * Register sequence from espressif/es7210 es7210_config_codec
	 * (LCKFB 04-audio_es7210). TDM is left off: Zephyr i2s_esp32 is
	 * STD Philips stereo, so MIC1/MIC2 map to L/R.
	 */
	static const uint8_t tbl[][2] = {
	        {0x00, 0x32},
	        {0x09, 0x30}, /* power-up timing */
	        {0x0A, 0x30},
	        {0x23, 0x2A}, /* ADC12 HPF */
	        {0x22, 0x0A},
	        {0x21, 0x2A}, /* ADC34 HPF */
	        {0x20, 0x0A},
	        {0x11, 0x60}, /* 16-bit I2S */
	        {0x12, 0x00}, /* STD, not 1xFS TDM */
	        {0x40, 0xC3}, /* analog / VMID */
	        {0x41, 0x70}, /* MIC12 bias 2.87 V */
	        {0x42, 0x70},
	        {0x43, 0x1A}, /* PGA on + 30 dB */
	        {0x44, 0x1A},
	        {0x45, 0x1A},
	        {0x46, 0x1A},
	        {0x47, 0x08}, /* MIC1-4 analog on */
	        {0x48, 0x08},
	        {0x49, 0x08},
	        {0x4A, 0x08},
	        /* 16 kHz, MCLK = 256 * fs = 4.096 MHz */
	        {0x07, 0x20},
	        {0x02, 0xC1},
	        {0x04, 0x01},
	        {0x05, 0x00},
	        {0x06, 0x04}, /* DLL off */
	        {0x4B, 0x0F}, /* MIC12 bias / ADC / PGA on */
	        {0x4C, 0x0F},
	        {0x13, 0x00},
	        {0x14, 0x00}, /* unmute */
	        {0x15, 0x00},
	        {0x1B, 0xBF}, /* 0 dB digital volume */
	        {0x1C, 0xBF},
	        {0x1D, 0xBF},
	        {0x1E, 0xBF},
	        {0x00, 0x71},
	        {0x00, 0x41},
	};
	for (size_t i = 0; i < ARRAY_SIZE(tbl); i++) {
		err = codec_wr(adc_addr, tbl[i][0], tbl[i][1]);
		if (err) {
			return err;
		}
	}
	LOG_INF("es7210 ready id=0x%02x addr=0x%02x", id, adc_addr);
	return 0;
}

static int init(uint8_t volume)
{
#ifdef ASTER_SYSTEM_ESP_IDF
	i2c_dev = aster_idf_audio_i2c();
#else
	i2c_dev = DEVICE_DT_GET_OR_NULL(DT_CHOSEN(aster_audio_i2c));
#endif
	if (!i2c_dev || !device_is_ready(i2c_dev))
		return -ENODEV;
	int err = es8311_init(volume);
	if (!err && !IS_ENABLED(CONFIG_ASTER_AUDIO_ADC_ES8311)) {
		int adc_error = es7210_init();
		if (adc_error)
			LOG_WRN("es7210 init %d (playback still available)", adc_error);
	}
	return err;
}

static int set_rate(uint32_t rate)
{
	/* I2S supplies MCLK at 256 fs. Select the matching ES8311 DAC OSR. */
	return codec_wr(DAC_ADDR, 0x04, rate > 16000 ? 0x10 : 0x20);
}

static void identify(int init_error, uint8_t *dac_id, uint8_t *adc_id_out)
{
	(void)init_error;
	if (dac_id) {
		*dac_id = 0;
		if (i2c_dev && device_is_ready(i2c_dev)) {
			(void)codec_rd(DAC_ADDR, 0xFD, dac_id);
		}
	}
	if (adc_id_out) {
		*adc_id_out = 0;
		if (i2c_dev && device_is_ready(i2c_dev)) {
			(void)codec_rd(IS_ENABLED(CONFIG_ASTER_AUDIO_ADC_ES8311) ? DAC_ADDR
			                                                         : adc_addr,
			               IS_ENABLED(CONFIG_ASTER_AUDIO_ADC_ES8311) ? 0xFD : 0x3D,
			               adc_id_out);
		}
	}
}

const aster_audio_codec_ops_t aster_audio_codec = {
	.init = init,
	.enable = es8311_dac_on,
	.set_volume = es8311_apply_vol,
	.set_rate = set_rate,
	.identify = identify,
};

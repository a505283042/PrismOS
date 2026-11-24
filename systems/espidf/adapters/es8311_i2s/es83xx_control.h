/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdint.h>

struct device;
const struct device *aster_idf_audio_i2c(void);
int i2c_reg_write_byte(const struct device *device, uint8_t address, uint8_t reg, uint8_t value);
int i2c_reg_read_byte(const struct device *device, uint8_t address, uint8_t reg, uint8_t *value);

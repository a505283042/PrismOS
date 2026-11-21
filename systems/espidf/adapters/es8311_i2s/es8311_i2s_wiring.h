/* SPDX-License-Identifier: MIT */
#pragma once
#include "driver/i2s_std.h"

typedef struct {
    int i2c_port;
    int i2s_port;
    i2s_std_gpio_config_t pins;
} aster_es8311_i2s_wiring_t;

extern const aster_es8311_i2s_wiring_t aster_es8311_wiring;

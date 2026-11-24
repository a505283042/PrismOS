/* SPDX-License-Identifier: MIT */
#include "es8311_i2s_wiring.h"
#include "audio_port.h"

const struct gpio_dt_spec aster_idf_audio_pa = {.port = &aster_idf_gpio_device, .pin = 53};
const aster_es8311_i2s_wiring_t aster_es8311_wiring = {
    .i2c_port = 1,
    .i2s_port = I2S_NUM_0,
    .pins = {.mclk = 13, .bclk = 12, .ws = 10, .dout = 9, .din = 11},
};

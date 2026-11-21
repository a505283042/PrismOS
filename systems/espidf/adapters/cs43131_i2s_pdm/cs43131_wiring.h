/* SPDX-License-Identifier: MIT */
#pragma once
#include "driver/i2s_std.h"
#include <stdint.h>

/* CS43131 with a 24.576 MHz crystal and a separate PDM microphone. */
typedef struct {
    int i2c_port;
    uint8_t dac_address;
    gpio_num_t sda, scl, reset;
    int tx_port, rx_port;
    i2s_clock_src_t tx_clock;
    gpio_num_t bclk, ws, dout;
    gpio_num_t mic_clock, mic_data;
} aster_cs43131_wiring_t;

extern const aster_cs43131_wiring_t aster_cs43131_wiring;

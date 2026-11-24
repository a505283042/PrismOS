/* SPDX-License-Identifier: MIT */
#include "../main/board_pins.h"
#include "cs43131_wiring.h"

const aster_cs43131_wiring_t aster_cs43131_wiring = {
    .i2c_port = 1,
    .dac_address = FP_DAC_ADDRESS,
    .sda = FP_DAC_SDA,
    .scl = FP_DAC_SCL,
    .reset = FP_DAC_RESET,
    .tx_port = I2S_NUM_1,
    .rx_port = I2S_NUM_0,
    /* The default XTAL cannot clock 88.2/96 kHz with 256x MCLK on this board. */
    .tx_clock = I2S_CLK_SRC_DEFAULT,
    .bclk = FP_DAC_BCLK,
    .ws = FP_DAC_WS,
    .dout = FP_DAC_DOUT,
    .mic_clock = FP_MIC_CLOCK,
    .mic_data = FP_MIC_DATA,
};

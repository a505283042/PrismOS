/* SPDX-License-Identifier: MIT */
#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>
esp_err_t fakepod_input_init(void);
int fakepod_touch_read(uint32_t *x,uint32_t *y);

int fakepod_imu_read(float *ax,float *ay,float *az,float *gx,float *gy,float *gz);
bool fakepod_imu_available(void);

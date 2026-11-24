/* SPDX-License-Identifier: MIT */
#pragma once
#include "esp_err.h"
#include "sdmmc_cmd.h"

esp_err_t fakepod_storage_open_raw(sdmmc_card_t *card);
int fakepod_usb_storage(void *context);

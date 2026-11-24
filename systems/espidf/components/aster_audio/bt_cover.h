/* SPDX-License-Identifier: MIT */
#pragma once
#include <aster/player.h>
#include "esp_avrc_api.h"

void aster_bt_cover_event(esp_avrc_ct_cb_event_t event, const esp_avrc_ct_cb_param_t *p);
void aster_bt_cover_reset(void);
uint8_t aster_bt_cover_mask(void);
void aster_bt_cover_info(aster_player_info_t *info);

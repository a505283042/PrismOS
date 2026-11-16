/* SPDX-License-Identifier: MIT */
#pragma once
#include "esp_err.h"
#include <aster/player.h>
/* Called by the A2DP owner while Bluedroid is enabled. No extra task/heap. */
esp_err_t aster_bt_avrcp_start(void);
esp_err_t aster_bt_avrcp_stop(void);
void aster_bt_avrcp_poll(void);
int aster_bt_avrcp_command(const char *name);
void aster_bt_avrcp_status(void);
void aster_bt_avrcp_info(aster_player_info_t *info);

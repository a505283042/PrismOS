/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"
#include <aster/player.h>
int aster_bt_tx_register(void);
void aster_bt_tx_prepare(StreamBufferHandle_t queue);
void aster_bt_tx_link(bool connected,bool streaming,uint32_t rate);
int32_t aster_bt_tx_callback(uint8_t *data,int32_t length);
bool aster_bt_tx_active(void);
uint32_t aster_bt_tx_queued(void);
int aster_idf_bt_source_request(uint32_t command,const char *address);
int aster_idf_bt_source_list(aster_player_bt_list_t *out);

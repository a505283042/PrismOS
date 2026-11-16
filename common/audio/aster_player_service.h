/* SPDX-License-Identifier: MIT */
#pragma once
#include <aster/player.h>
/* Optional adapter, registered once at boot. Callbacks return native errno.
 * No callback may retain caller storage; control only queues bounded work. */
typedef struct {
 int (*info)(aster_player_info_t *info);
 int (*control)(const aster_player_request_t *request);
 int (*bt_list)(aster_player_bt_list_t *list);
} aster_player_ops_t;
int aster_player_service_register(const aster_player_ops_t *ops);
int aster_player_service_available(void);
int aster_player_service_info(aster_player_info_t *info);
int aster_player_service_control(const aster_player_request_t *request);
int aster_player_service_bt_list(aster_player_bt_list_t *list);

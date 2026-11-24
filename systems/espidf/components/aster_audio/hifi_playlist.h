/* SPDX-License-Identifier: MIT
 * Canonical global HiFi song order for firmware-owned transport.
 * The five HiFi APP browsing scopes do not create playback queues.
 * Uses the verified HIF2 catalog of the installed aster.hifi APP.
 */
#ifndef ASTER_HIFI_PLAYLIST_H
#define ASTER_HIFI_PLAYLIST_H
#include <stdbool.h>
#include <stdint.h>
#include "aster_media.h"

#define HIFI_QUEUE_SEQUENCE 0u
#define HIFI_QUEUE_REPEAT_ALL 1u
#define HIFI_QUEUE_REPEAT_ONE 2u
#define HIFI_QUEUE_SHUFFLE 3u

/* Returns true if out received a playable SD path.
 * Only call from the non-audio control worker while media is stopped/ended. */
bool hifi_playlist_choose(const char *current, uint32_t mode,
                          int direction, bool automatic,
                          char out[ASTER_MEDIA_PATH_MAX]);
void hifi_playlist_reset_shuffle(void);
void hifi_playlist_seed(uint32_t entropy);
#endif

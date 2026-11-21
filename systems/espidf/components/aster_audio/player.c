/* SPDX-License-Identifier: MIT */
#include "sdkconfig.h"
#include "aster_player_service.h"
#include "aster_media.h"
#include "aster_audio_output.h"
#include "bt_source.h"
#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
bool aster_idf_bt_audio_info(aster_player_info_t *info);
const char *aster_idf_bt_device_name(void);
int aster_idf_bt_audio_control(uint32_t command);

static int info(aster_player_info_t *out)
{
    out->capabilities = ASTER_PLAYER_CAP_WAV | ASTER_PLAYER_CAP_FLAC;
#ifdef ASTER_AUDIO_SPECTRUM_ENABLED
    out->capabilities |= ASTER_PLAYER_CAP_SPECTRUM;
#endif
#if CONFIG_BT_ENABLED && CONFIG_BT_CLASSIC_ENABLED
    out->capabilities |= ASTER_PLAYER_CAP_BT_SINK | ASTER_PLAYER_CAP_REMOTE |
                         ASTER_PLAYER_CAP_BT_SOURCE | ASTER_PLAYER_CAP_SYSTEM_OUTPUT;
#endif
    if (aster_idf_bt_audio_info(out)) {
#ifdef ASTER_AUDIO_SPECTRUM_ENABLED
        if (out->state == ASTER_PLAYER_PLAYING)
            aster_audio_output_spectrum(ASTER_AUDIO_OUTPUT_WIRED, out->reserved);
#endif
        return 0;
    }
    snprintf(out->device, sizeof(out->device), "%s", aster_idf_bt_device_name());
    aster_media_snapshot_t media;
    int r = aster_media_snapshot(&media);
    if (r)
        return r;
    out->mode = ASTER_PLAYER_LOCAL;
    out->state = media.state;
    out->error = media.error;
    out->rate = media.rate;
    out->bits = media.bits;
    out->channels = media.channels;
    if (media.rate) {
        out->position_ms = (uint32_t)(media.frame * 1000 / media.rate);
        out->duration_ms = (uint32_t)(media.total_frames * 1000 / media.rate);
    }
    memcpy(out->path, media.path, sizeof(out->path));
    if (media.endpoint == ASTER_AUDIO_OUTPUT_BLUETOOTH)
        out->flags |= ASTER_PLAYER_F_BT_OUTPUT;
#ifdef ASTER_AUDIO_SPECTRUM_ENABLED
    if (out->state == ASTER_PLAYER_PLAYING)
        aster_audio_output_spectrum(media.endpoint, out->reserved);
#endif
    return 0;
}

static int control(const aster_player_request_t *request)
{
    if (request->command >= ASTER_PLAYER_SOURCE_ON)
        return aster_idf_bt_source_request(request->command, request->path);
    if (request->command == ASTER_PLAYER_BT_ON || request->command == ASTER_PLAYER_BT_OFF ||
        request->command == ASTER_PLAYER_NEXT || request->command == ASTER_PLAYER_PREVIOUS)
        return aster_idf_bt_audio_control(request->command);
    aster_player_info_t bt = {0};
    if (aster_idf_bt_audio_info(&bt)) {
        if (request->command == ASTER_PLAYER_SEEK)
            return -ENOTSUP;
        if (request->command == ASTER_PLAYER_PAUSE || request->command == ASTER_PLAYER_RESUME)
            return aster_idf_bt_audio_control(request->command);
        return -EBUSY;
    }
    switch (request->command) {
    case ASTER_PLAYER_PLAY_FILE:
        if (request->value == ASTER_PLAYER_OUTPUT_SYSTEM) {
#if CONFIG_BT_ENABLED && CONFIG_BT_CLASSIC_ENABLED
            return aster_media_play(request->path, ASTER_MEDIA_OUTPUT_SYSTEM);
#else
            return -ENOTSUP;
#endif
        }
        if (request->value > ASTER_AUDIO_OUTPUT_BLUETOOTH)
            return -EINVAL;
        if (request->value == ASTER_AUDIO_OUTPUT_BLUETOOTH) {
            aster_player_bt_list_t list = {0};
            int r = aster_idf_bt_source_list(&list);
            if (r)
                return r;
            if (list.state != ASTER_PLAYER_BT_CONNECTED || !list.output_rate)
                return -ENOTCONN;
        }
        return aster_media_play(request->path, request->value ? request->value : ASTER_AUDIO_OUTPUT_WIRED);
    case ASTER_PLAYER_PAUSE:
        return aster_media_pause();
    case ASTER_PLAYER_RESUME:
        return aster_media_resume();
    case ASTER_PLAYER_STOP:
        return aster_media_stop();
    case ASTER_PLAYER_SEEK:
        return aster_media_seek(request->value);
    default:
        return -ENOTSUP;
    }
}

int aster_idf_player_register(void)
{
    static const aster_player_ops_t ops = {
        .info = info, .control = control, .bt_list = aster_idf_bt_source_list};
    return aster_player_service_register(&ops);
}

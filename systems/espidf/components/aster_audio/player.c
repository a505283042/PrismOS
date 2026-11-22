/* SPDX-License-Identifier: MIT */
#include "sdkconfig.h"
#include "aster_player_service.h"
#include "aster_media.h"
#include "aster_audio_output.h"
#include "bt_source.h"
#include "hifi_playlist.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdatomic.h>
bool aster_idf_bt_audio_info(aster_player_info_t *info);
const char *aster_idf_bt_device_name(void);
int aster_idf_bt_audio_control(uint32_t command);

/* FAT directory order is the order shown by HiFiPlayer. This task is the
 * only automatic-next owner once the capability is advertised: the APP no
 * longer needs to stay open to maintain a local playback queue. */
static atomic_bool auto_next_running;
static atomic_uint global_mode = ATOMIC_VAR_INIT(HIFI_QUEUE_REPEAT_ALL);
static atomic_bool mode_needs_save;
static atomic_int next_request;
static atomic_uint playback_intent;

/* Persist only a small setting in ESP-IDF NVS, not full song paths.
 * The global queue state is owned by firmware; no APP may own background EOF. */
static void mode_restore(void)
{
    if (nvs_flash_init() != ESP_OK) return;
    nvs_handle_t handle=0;
    if (nvs_open("aster_hifi",NVS_READONLY,&handle)!=ESP_OK)return;
    uint8_t stored=HIFI_QUEUE_REPEAT_ALL;
    if (nvs_get_u8(handle,"queue_mode",&stored)==ESP_OK &&
        stored<=HIFI_QUEUE_SHUFFLE)
        atomic_store(&global_mode,stored);
    nvs_close(handle);
}
static void mode_save_if_idle(uint32_t media_state)
{
    if (!atomic_load(&mode_needs_save) ||
        media_state==ASTER_MEDIA_PLAYING || media_state==ASTER_MEDIA_OPENING ||
        media_state==ASTER_MEDIA_STOPPING)return;
    uint8_t value=(uint8_t)atomic_load(&global_mode);
    nvs_handle_t handle=0;
    if (nvs_open("aster_hifi",NVS_READWRITE,&handle)!=ESP_OK)return;
    esp_err_t result=nvs_set_u8(handle,"queue_mode",value);
    if(result==ESP_OK)result=nvs_commit(handle);
    nvs_close(handle);
    if(result==ESP_OK)atomic_store(&mode_needs_save,false);
}

static const char *music_basename(const char *path)
{
    if (!strncmp(path, "/SD:/music/", 11))
        return path + 11;
    if (!strncmp(path, "/SD/music/", 10))
        return path + 10;
    return NULL;
}

static bool music_filename(const char *name)
{
    const char *ext = strrchr(name, '.');
    if (!ext || !ext[1])
        return false;
    char tail[5] = {0};
    size_t length = strlen(++ext);
    if (length >= sizeof(tail))
        return false;
    for (size_t i = 0; i < length; i++)
        tail[i] = ext[i] >= 'A' && ext[i] <= 'Z' ? (char)(ext[i] + ('a' - 'A')) : ext[i];
    return !strcmp(tail, "flac") || !strcmp(tail, "mp3") || !strcmp(tail, "wav");
}

/* Do not allocate a full firmware playlist: locate the following filename
 * with one bounded directory pass, wrapping at the last entry. */
static bool music_successor(const char *path, char out[ASTER_MEDIA_PATH_MAX])
{
    const char *current = music_basename(path);
    if (!current || !*current)
        return false;
    DIR *directory = opendir("/SD/music");
    if (!directory) {
        ESP_LOGW("hifi_autoplay", "cannot open music directory: errno=%d", errno);
        return false;
    }
    char first[ASTER_MEDIA_PATH_MAX] = {0};
    char chosen[ASTER_MEDIA_PATH_MAX] = {0};
    bool found_current = false;
    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
#ifdef DT_DIR
        if (entry->d_type == DT_DIR)
            continue;
#endif
        const char *name = entry->d_name;
        size_t length = strlen(name);
        if (name[0] == '.' || !music_filename(name) ||
            length + sizeof("/SD:/music/") > ASTER_MEDIA_PATH_MAX)
            continue;
        if (!first[0])
            memcpy(first, name, length + 1);
        if (found_current) {
            memcpy(chosen, name, length + 1);
            break;
        }
        if (!strcmp(name, current))
            found_current = true;
    }
    closedir(directory);
    if (!found_current)
        return false;
    if (!chosen[0] && first[0] && strcmp(first, current))
        memcpy(chosen, first, strlen(first) + 1);
    if (!chosen[0])
        return false; /* Only one file, or the current entry disappeared. */
    int n = snprintf(out, ASTER_MEDIA_PATH_MAX, "/SD:/music/%s", chosen);
    return n > 0 && (unsigned)n < ASTER_MEDIA_PATH_MAX;
}

static bool media_busy(uint32_t state)
{
    return state==ASTER_MEDIA_OPENING || state==ASTER_MEDIA_PLAYING ||
           state==ASTER_MEDIA_PAUSED || state==ASTER_MEDIA_STOPPING;
}

/* All SD reads and queue decisions happen in this lower-priority task only.
 * Manual NEXT while decoding first requests STOP and waits for media worker
 * to release SD + output resources, then consults the HIF2 catalog. */
static void local_auto_next_task(void *ctx)
{
    (void)ctx;
    char armed[ASTER_MEDIA_PATH_MAX]={0};
    char manual_path[ASTER_MEDIA_PATH_MAX]={0};
    int manual_direction=0;
    uint32_t manual_generation=0;
    uint32_t previous_mode=atomic_load(&global_mode);
    TickType_t ended_since=0;
    for (;;) {
        aster_media_snapshot_t snapshot={.size=sizeof(snapshot),.version=1};
        if (aster_media_snapshot(&snapshot)) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
        uint32_t mode=atomic_load(&global_mode);
        if(mode!=previous_mode){
            previous_mode=mode;
            hifi_playlist_reset_shuffle();
        }
        mode_save_if_idle(snapshot.state);
        int step=atomic_exchange(&next_request,0);
        if (step) {
            if (music_basename(snapshot.path) && *music_basename(snapshot.path)) {
                snprintf(manual_path,sizeof(manual_path),"%s",snapshot.path);
                manual_direction=step;
                manual_generation=atomic_load(&playback_intent);
                armed[0]=0;
                ended_since=0;
                if(media_busy(snapshot.state))
                    (void)aster_media_stop();
            }
        }
        if (manual_direction) {
            if (manual_generation!=atomic_load(&playback_intent)) {
                manual_direction=0;
            } else if (!media_busy(snapshot.state)) {
                char next[ASTER_MEDIA_PATH_MAX]={0};
                if(hifi_playlist_choose(manual_path,mode,manual_direction,false,next)) {
                    int result=aster_media_play(next,ASTER_MEDIA_OUTPUT_SYSTEM);
                    if(result==-EBUSY) {
                        vTaskDelay(pdMS_TO_TICKS(80));
                        continue;
                    }
                    if(result)
                        ESP_LOGW("hifi_autoplay","manual step failed: %d",result);
                    else
                        ESP_LOGI("hifi_autoplay","manual: %s",next);
                } else {
                    ESP_LOGW("hifi_autoplay","manual no successor: %s",manual_path);
                }
                manual_direction=0;
            }
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
        if(snapshot.state==ASTER_MEDIA_OPENING || snapshot.state==ASTER_MEDIA_PLAYING ||
           snapshot.state==ASTER_MEDIA_PAUSED) {
            const char *name=music_basename(snapshot.path);
            if(name && *name)
                snprintf(armed,sizeof(armed),"%s",snapshot.path);
            else armed[0]=0;
            ended_since=0;
        } else if(snapshot.state==ASTER_MEDIA_ENDED && armed[0] &&
                  !strcmp(armed,snapshot.path)) {
            TickType_t now=xTaskGetTickCount();
            if(!ended_since)ended_since=now;
            /* Legacy APPs may still attempt their own EOF transition.
             * New APPs use CAP_AUTO_NEXT + CAP_QUEUE_MODE and defer to us. */
            if((TickType_t)(now-ended_since)>=pdMS_TO_TICKS(500)){
                armed[0]=0;
                ended_since=0;
                char next[ASTER_MEDIA_PATH_MAX]={0};
                if(hifi_playlist_choose(snapshot.path,mode,1,true,next)){
                    if(atomic_load(&next_request) || snapshot.state!=ASTER_MEDIA_ENDED)
                        continue;
                    int result=aster_media_play(next,ASTER_MEDIA_OUTPUT_SYSTEM);
                    if(result)
                        ESP_LOGW("hifi_autoplay","next failed: %d",result);
                    else
                        ESP_LOGI("hifi_autoplay","next: %s mode=%u",next,mode);
                } else {
                    ESP_LOGI("hifi_autoplay","end of global queue mode=%u path=%s",
                             mode,snapshot.path);
                }
            }
        } else if(snapshot.state!=ASTER_MEDIA_ENDED) {
            armed[0]=0;
            ended_since=0;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

static int info(aster_player_info_t *out)
{
    out->capabilities = ASTER_PLAYER_CAP_WAV | ASTER_PLAYER_CAP_FLAC;
    if (atomic_load(&auto_next_running))
        out->capabilities |= ASTER_PLAYER_CAP_AUTO_NEXT | ASTER_PLAYER_CAP_QUEUE_MODE;
    out->reserved[3]=atomic_load(&global_mode);
#ifdef ASTER_AUDIO_SPECTRUM_ENABLED
    out->capabilities |= ASTER_PLAYER_CAP_SPECTRUM;
#endif
#ifdef ASTER_IDF_BT_AUDIO
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
    if (request->command == ASTER_PLAYER_BT_ON ||
        request->command == ASTER_PLAYER_BT_OFF)
        return aster_idf_bt_audio_control(request->command);
    if (request->command >= ASTER_PLAYER_SOURCE_ON &&
        request->command <= ASTER_PLAYER_BT_DISCONNECT)
        return aster_idf_bt_source_request(request->command, request->path);

    aster_player_info_t bt = {0};
    if (aster_idf_bt_audio_info(&bt)) {
        if (request->command == ASTER_PLAYER_SEEK ||
            request->command == ASTER_PLAYER_SET_QUEUE_MODE)
            return -ENOTSUP;
        if (request->command == ASTER_PLAYER_PAUSE ||
            request->command == ASTER_PLAYER_RESUME ||
            request->command == ASTER_PLAYER_NEXT ||
            request->command == ASTER_PLAYER_PREVIOUS)
            return aster_idf_bt_audio_control(request->command);
        return -EBUSY;
    }
    if (request->command == ASTER_PLAYER_SET_QUEUE_MODE) {
        if (!atomic_load(&auto_next_running)) return -ENOTSUP;
        if (request->value > HIFI_QUEUE_SHUFFLE) return -EINVAL;
        atomic_store(&global_mode,request->value);
        atomic_store(&mode_needs_save,true);
        return 0;
    }
    if (request->command == ASTER_PLAYER_NEXT ||
        request->command == ASTER_PLAYER_PREVIOUS) {
        if (!atomic_load(&auto_next_running)) return -ENOTSUP;
        atomic_store(&next_request,request->command == ASTER_PLAYER_NEXT ? 1 : -1);
        return 0;
    }
    switch (request->command) {
    case ASTER_PLAYER_PLAY_FILE:
        atomic_fetch_add(&playback_intent,1u);
        atomic_store(&next_request,0);
        if (request->value == ASTER_PLAYER_OUTPUT_SYSTEM) {
#ifdef ASTER_IDF_BT_AUDIO
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
        atomic_fetch_add(&playback_intent,1u);
        atomic_store(&next_request,0);
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
#ifdef ASTER_IDF_BT_AUDIO
    int err = aster_bt_tx_register();
    if (err)
        return err;
#endif
    int media = aster_media_init();
    if (media)
        return media;
    mode_restore();
    hifi_playlist_seed(esp_random());
    int registered = aster_player_service_register(&ops);
    if (registered)
        return registered;
    if (!atomic_load(&auto_next_running) &&
        xTaskCreate(local_auto_next_task, "hifi_autoplay", 12288, NULL, 4, NULL) == pdPASS) {
        atomic_store(&auto_next_running, true);
        ESP_LOGI("hifi_autoplay", "global HIF2 queue enabled mode=%u",
                 (unsigned)atomic_load(&global_mode));
    }
    else if (!atomic_load(&auto_next_running))
        ESP_LOGW("hifi_autoplay", "task unavailable; APP will handle EOF while open");
    return 0;
}

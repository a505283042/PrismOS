/* SPDX-License-Identifier: MIT */
/* AVRCP CT transport/metadata + TG absolute volume. Callbacks only publish
 * bounded state; the A2DP owner applies remote volume and sends key actions. */
#include "bt_avrcp.h"
#include "aster_platform.h"
#include "esp_avrc_api.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdatomic.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "bt_avrcp";
static atomic_bool ct_up, tg_up, ct_ready, tg_ready, ct_down, tg_down, stopping;
static atomic_bool volume_notify;
static atomic_int remote_volume, pending_key, key_ack, playback, control_error;
static atomic_uint wire_volume, acks, key_labels, retired_pairs;
/* Keep clear of labels 0–3 and 6 used by metadata and notifications. */
static const uint8_t key_pairs[] = {0x54, 0x87, 0xa9, 0xcb, 0xed};

enum { KEY_PAIR_COUNT = sizeof(key_pairs), ALL_KEY_PAIRS = (1u << KEY_PAIR_COUNT) - 1 };

static bool ct_initialized, tg_initialized;
static int last_percent, active_key, key_stage, key_pair, next_pair;
static int64_t key_deadline;
static char title[192], artist[192], album[192];
static portMUX_TYPE metadata_lock = portMUX_INITIALIZER_UNLOCKED;

static bool wait_state(atomic_bool *state)
{
    int64_t end = esp_timer_get_time() + 3000000;
    while (!atomic_load(state) && esp_timer_get_time() < end)
        vTaskDelay(pdMS_TO_TICKS(10));
    return atomic_load(state);
}

static void result(esp_err_t error)
{
    if (error) {
        atomic_store(&control_error, error);
        ESP_LOGW(TAG, "request: %s", esp_err_to_name(error));
    }
}

static void clear_metadata(void)
{
    portENTER_CRITICAL(&metadata_lock);
    title[0] = artist[0] = album[0] = 0;
    portEXIT_CRITICAL(&metadata_lock);
}

static void metadata_request(void)
{
    clear_metadata();
    result(esp_avrc_ct_send_metadata_cmd(0, ESP_AVRC_MD_ATTR_TITLE | ESP_AVRC_MD_ATTR_ARTIST |
                                                ESP_AVRC_MD_ATTR_ALBUM));
}

static void ct_callback(esp_avrc_ct_cb_event_t event, esp_avrc_ct_cb_param_t *p)
{
    if (event == ESP_AVRC_CT_PROF_STATE_EVT) {
        if (p->avrc_ct_init_stat.state == ESP_AVRC_INIT_SUCCESS)
            atomic_store(&ct_ready, true);
        if (p->avrc_ct_init_stat.state == ESP_AVRC_DEINIT_SUCCESS)
            atomic_store(&ct_down, true);
        return;
    }
    if (atomic_load(&stopping))
        return;
    if (event == ESP_AVRC_CT_CONNECTION_STATE_EVT) {
        atomic_store(&ct_up, p->conn_stat.connected);
        clear_metadata();
        atomic_store(&retired_pairs, 0);
        atomic_store(&key_labels, 0);
        atomic_store(&playback, ESP_AVRC_PLAYBACK_ERROR);
        if (p->conn_stat.connected) {
            result(esp_avrc_ct_send_get_rn_capabilities_cmd(1));
            metadata_request();
            result(esp_avrc_ct_send_get_play_status_cmd(6));
        }
        ESP_LOGI(TAG, "controller connected=%d", p->conn_stat.connected);
    } else if (event == ESP_AVRC_CT_METADATA_RSP_EVT && p->meta_rsp.attr_text &&
               p->meta_rsp.attr_length > 0) {
        char *dest = p->meta_rsp.attr_id == ESP_AVRC_MD_ATTR_TITLE    ? title
                     : p->meta_rsp.attr_id == ESP_AVRC_MD_ATTR_ARTIST ? artist
                     : p->meta_rsp.attr_id == ESP_AVRC_MD_ATTR_ALBUM  ? album
                                                                      : NULL;
        if (!dest)
            return;
        size_t n = (size_t)p->meta_rsp.attr_length;
        if (n >= sizeof(title)) {
            n = sizeof(title) - 1;
            /* Do not retain an incomplete UTF-8 sequence at the truncation boundary. */
            while (n && (p->meta_rsp.attr_text[n] & 0xc0) == 0x80)
                n--;
        }
        portENTER_CRITICAL(&metadata_lock);
        memcpy(dest, p->meta_rsp.attr_text, n);
        dest[n] = 0;
        portEXIT_CRITICAL(&metadata_lock);
    } else if (event == ESP_AVRC_CT_GET_RN_CAPABILITIES_RSP_EVT) {
        esp_avrc_rn_evt_cap_mask_t caps = p->get_rn_caps_rsp.evt_set;
        if (esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_TEST, &caps,
                                               ESP_AVRC_RN_TRACK_CHANGE))
            result(esp_avrc_ct_send_register_notification_cmd(2, ESP_AVRC_RN_TRACK_CHANGE, 0));
        if (esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_TEST, &caps,
                                               ESP_AVRC_RN_PLAY_STATUS_CHANGE))
            result(
                esp_avrc_ct_send_register_notification_cmd(3, ESP_AVRC_RN_PLAY_STATUS_CHANGE, 0));
    } else if (event == ESP_AVRC_CT_CHANGE_NOTIFY_EVT) {
        if (p->change_ntf.event_id == ESP_AVRC_RN_TRACK_CHANGE) {
            metadata_request();
            result(esp_avrc_ct_send_register_notification_cmd(2, ESP_AVRC_RN_TRACK_CHANGE, 0));
        } else if (p->change_ntf.event_id == ESP_AVRC_RN_PLAY_STATUS_CHANGE) {
            atomic_store(&playback, p->change_ntf.event_parameter.playback);
            result(
                esp_avrc_ct_send_register_notification_cmd(3, ESP_AVRC_RN_PLAY_STATUS_CHANGE, 0));
        }
    } else if (event == ESP_AVRC_CT_PLAY_STATUS_RSP_EVT) {
        atomic_store(&playback, p->play_status_rsp.play_status);
    } else if (event == ESP_AVRC_CT_PASSTHROUGH_RSP_EVT) {
        unsigned labels = atomic_load(&key_labels);
        bool release = p->psth_rsp.key_state == ESP_AVRC_PT_CMD_STATE_RELEASED;
        unsigned expected_label = release ? labels >> 4 : labels & 15;
        if (labels && p->psth_rsp.tl == expected_label &&
            atomic_load(&pending_key) == p->psth_rsp.key_code + 1) {
            if (p->psth_rsp.rsp_code != ESP_AVRC_RSP_ACCEPT)
                atomic_store(&control_error, ESP_FAIL);
            if (!release) {
                int expected = 0;
                atomic_compare_exchange_strong(&key_ack, &expected, 1);
            } else
                atomic_store(&key_ack, 2);
            atomic_fetch_add(&acks, 1);
        }
    }
}

static void tg_callback(esp_avrc_tg_cb_event_t event, esp_avrc_tg_cb_param_t *p)
{
    if (event == ESP_AVRC_TG_PROF_STATE_EVT) {
        if (p->avrc_tg_init_stat.state == ESP_AVRC_INIT_SUCCESS)
            atomic_store(&tg_ready, true);
        if (p->avrc_tg_init_stat.state == ESP_AVRC_DEINIT_SUCCESS)
            atomic_store(&tg_down, true);
        return;
    }
    if (atomic_load(&stopping))
        return;
    if (event == ESP_AVRC_TG_CONNECTION_STATE_EVT) {
        atomic_store(&tg_up, p->conn_stat.connected);
        atomic_store(&volume_notify, false);
        atomic_store(&remote_volume, -1);
    } else if (event == ESP_AVRC_TG_SET_ABSOLUTE_VOLUME_CMD_EVT) {
        atomic_store(&remote_volume, p->set_abs_vol.volume & 127);
    } else if (event == ESP_AVRC_TG_REGISTER_NOTIFICATION_EVT &&
               p->reg_ntf.event_id == ESP_AVRC_RN_VOLUME_CHANGE) {
        esp_avrc_rn_param_t value = {.volume = atomic_load(&wire_volume)};
        esp_err_t e =
            esp_avrc_tg_send_rn_rsp(ESP_AVRC_RN_VOLUME_CHANGE, ESP_AVRC_RN_RSP_INTERIM, &value);
        atomic_store(&volume_notify, e == ESP_OK);
        result(e);
    }
}

esp_err_t aster_bt_avrcp_start(void)
{
    atomic_store(&stopping, false);
    atomic_store(&ct_up, false);
    atomic_store(&tg_up, false);
    atomic_store(&ct_ready, false);
    atomic_store(&tg_ready, false);
    atomic_store(&ct_down, false);
    atomic_store(&tg_down, false);
    atomic_store(&volume_notify, false);
    atomic_store(&remote_volume, -1);
    atomic_store(&pending_key, 0);
    atomic_store(&key_ack, 0);
    atomic_store(&key_labels, 0);
    atomic_store(&retired_pairs, 0);
    atomic_store(&acks, 0);
    atomic_store(&control_error, 0);
    atomic_store(&playback, ESP_AVRC_PLAYBACK_ERROR);
    clear_metadata();
    last_percent = aster_plat_audio_get_volume();
    atomic_store(&wire_volume, (last_percent * 127 + 50) / 100);
    active_key = key_stage = key_pair = next_pair = 0;
    esp_err_t e = esp_avrc_ct_register_callback(ct_callback);
    if (e)
        return e;
    e = esp_avrc_tg_register_callback(tg_callback);
    if (e)
        return e;
    e = esp_avrc_ct_init();
    if (e)
        return e;
    ct_initialized = true;
    if (!wait_state(&ct_ready))
        return ESP_ERR_TIMEOUT;
    e = esp_avrc_tg_init();
    if (e)
        return e;
    tg_initialized = true;
    if (!wait_state(&tg_ready))
        return ESP_ERR_TIMEOUT;
    esp_avrc_rn_evt_cap_mask_t caps = {0};
    esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_SET, &caps, ESP_AVRC_RN_VOLUME_CHANGE);
    return esp_avrc_tg_set_rn_evt_cap(&caps);
}

esp_err_t aster_bt_avrcp_stop(void)
{
    atomic_store(&stopping, true);
    esp_err_t error = ESP_OK, e;
    /* IDF requires AVRCP deinitialization before A2DP deinitialization. */
    if (tg_initialized) {
        e = esp_avrc_tg_deinit();
        if (e)
            error = e;
        else if (!wait_state(&tg_down))
            error = ESP_ERR_TIMEOUT;
        tg_initialized = false;
    }
    if (ct_initialized) {
        e = esp_avrc_ct_deinit();
        if (e)
            error = e;
        else if (!wait_state(&ct_down))
            error = ESP_ERR_TIMEOUT;
        ct_initialized = false;
    }
    atomic_store(&ct_up, false);
    atomic_store(&tg_up, false);
    atomic_store(&pending_key, 0);
    return error;
}

static void finish_key(void)
{
    atomic_store(&key_labels, 0);
    key_stage = 0;
    atomic_store(&pending_key, 0);
}

static void key_timeout(const char *phase)
{
    atomic_fetch_or(&retired_pairs, 1u << key_pair);
    atomic_store(&control_error, ESP_ERR_TIMEOUT);
    ESP_LOGW(TAG, "remote key 0x%02x %s timed out (labels 0x%02x)", active_key, phase,
             (unsigned)key_pairs[key_pair]);
}

void aster_bt_avrcp_poll(void)
{
    int remote = atomic_exchange(&remote_volume, -1);
    if (remote >= 0 && atomic_load(&tg_up)) {
        int percent = (remote * 100 + 63) / 127;
        int e = aster_plat_audio_set_volume(percent);
        if (e)
            atomic_store(&control_error, e);
        else {
            last_percent = percent;
            atomic_store(&wire_volume, remote);
        }
    }
    int percent = aster_plat_audio_get_volume();
    if (percent != last_percent) {
        last_percent = percent;
        atomic_store(&wire_volume, (percent * 127 + 50) / 100);
        if (atomic_load(&tg_up) && atomic_exchange(&volume_notify, false)) {
            esp_avrc_rn_param_t value = {.volume = atomic_load(&wire_volume)};
            result(esp_avrc_tg_send_rn_rsp(ESP_AVRC_RN_VOLUME_CHANGE, ESP_AVRC_RN_RSP_CHANGED,
                                           &value));
        }
    }
    int key = atomic_load(&pending_key);
    if (!atomic_load(&ct_up)) {
        finish_key();
        return;
    }
    if (!key)
        return;
    int64_t now = esp_timer_get_time();
    if (!key_stage) {
        unsigned retired = atomic_load(&retired_pairs);
        if (retired == ALL_KEY_PAIRS) {
            finish_key();
            return;
        }
        while (retired & (1u << next_pair))
            next_pair = (next_pair + 1) % KEY_PAIR_COUNT;
        key_pair = next_pair;
        next_pair = (next_pair + 1) % KEY_PAIR_COUNT;
        active_key = key - 1;
        atomic_store(&key_ack, 0);
        atomic_store(&key_labels, key_pairs[key_pair]);
        esp_err_t e = esp_avrc_ct_send_passthrough_cmd(key_pairs[key_pair] & 15, active_key,
                                                       ESP_AVRC_PT_CMD_STATE_PRESSED);
        if (e) {
            result(e);
            finish_key();
            return;
        }
        key_stage = 1;
        key_deadline = now + 1000000;
    } else if (key_stage == 1 && (atomic_load(&key_ack) == 1 || now >= key_deadline)) {
        /* Always release, even when the press acknowledgement was lost. */
        if (atomic_load(&key_ack) != 1)
            key_timeout("press");
        esp_err_t e = esp_avrc_ct_send_passthrough_cmd(key_pairs[key_pair] >> 4, active_key,
                                                       ESP_AVRC_PT_CMD_STATE_RELEASED);
        if (e) {
            atomic_fetch_or(&retired_pairs, 1u << key_pair);
            result(e);
            finish_key();
            return;
        }
        key_stage = 2;
        key_deadline = now + 1000000;
    } else if (key_stage == 2 && (atomic_load(&key_ack) == 2 || now >= key_deadline)) {
        if (atomic_load(&key_ack) != 2)
            key_timeout("release");
        finish_key();
    }
}

int aster_bt_avrcp_command(const char *name)
{
    int key = !strcmp(name, "play")       ? ESP_AVRC_PT_CMD_PLAY
              : !strcmp(name, "pause")    ? ESP_AVRC_PT_CMD_PAUSE
              : !strcmp(name, "next")     ? ESP_AVRC_PT_CMD_FORWARD
              : !strcmp(name, "previous") ? ESP_AVRC_PT_CMD_BACKWARD
                                          : -1;
    if (key < 0)
        return -EINVAL;
    if (atomic_load(&stopping) || !atomic_load(&ct_up))
        return -ENOTCONN;
    if (atomic_load(&retired_pairs) == ALL_KEY_PAIRS)
        return -ETIMEDOUT;
    int expected = 0;
    return atomic_compare_exchange_strong(&pending_key, &expected, key + 1) ? 0 : -EBUSY;
}

void aster_bt_avrcp_status(void)
{
    char t[sizeof(title)], a[sizeof(artist)], b[sizeof(album)];
    portENTER_CRITICAL(&metadata_lock);
    memcpy(t, title, sizeof(t));
    memcpy(a, artist, sizeof(a));
    memcpy(b, album, sizeof(b));
    portEXIT_CRITICAL(&metadata_lock);
    printf("AVRCP ct=%d tg=%d playback=%d busy=%d acks=%u volume127=%u error=%d\n",
           atomic_load(&ct_up), atomic_load(&tg_up), atomic_load(&playback),
           atomic_load(&pending_key) != 0, atomic_load(&acks), atomic_load(&wire_volume),
           atomic_load(&control_error));
    printf("title=%s\nartist=%s\nalbum=%s\n", t, a, b);
}

static void text_copy(char *out, size_t capacity, const char *in)
{
    size_t n = strlen(in);
    if (n >= capacity) {
        n = capacity - 1;
        while (n && ((unsigned char)in[n] & 0xc0) == 0x80)
            n--;
    }
    memcpy(out, in, n);
    out[n] = 0;
}

void aster_bt_avrcp_info(aster_player_info_t *info)
{
    portENTER_CRITICAL(&metadata_lock);
    text_copy(info->title, sizeof(info->title), title);
    text_copy(info->artist, sizeof(info->artist), artist);
    text_copy(info->album, sizeof(info->album), album);
    portEXIT_CRITICAL(&metadata_lock);
    if (atomic_load(&ct_up)) {
        info->flags |= ASTER_PLAYER_F_REMOTE;
        int state = atomic_load(&playback);
        if (state == ESP_AVRC_PLAYBACK_PLAYING)
            info->state = ASTER_PLAYER_PLAYING;
        else if (state == ESP_AVRC_PLAYBACK_PAUSED || state == ESP_AVRC_PLAYBACK_STOPPED)
            info->state = ASTER_PLAYER_PAUSED;
    }
}

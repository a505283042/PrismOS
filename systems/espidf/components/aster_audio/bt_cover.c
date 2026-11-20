/* SPDX-License-Identifier: MIT */
#include "sdkconfig.h"
#include "bt_cover.h"
#if CONFIG_BT_AVRCP_CT_COVER_ART_ENABLED
#include "aster_os.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <errno.h>
#include <stdio.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

/* The phone supplies a BIP JPEG thumbnail. Keep at most two files in the
 * read-only music tree. Only the low-priority writer touches the SD card. */
#define COVER_DIR "/music/.aster-bluetooth"
enum { COVER_LIMIT = 64 * 1024, HANDLE = ESP_AVRC_CA_IMAGE_HANDLE_LEN };
static const char *const paths[] = {COVER_DIR "/cover0.jpg", COVER_DIR "/cover1.jpg"};
struct thumbnail {
    uint32_t epoch;
    size_t length;
    uint8_t data[COVER_LIMIT];
};

/* The AVRCP callback owns the OBEX transfer. One writer drains the newest
 * completed object. A different handle or a disconnect invalidates old jobs. */
static atomic_bool linked;
static bool fetching, failed;
static uint8_t wanted[HANDLE], active[HANDLE];
static struct thumbnail *incoming, *pending;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t epoch = 1, saved, revision;
static unsigned slot;
static bool writer;
static int64_t confirm_by;

static int write_thumbnail(unsigned next, const struct thumbnail *job)
{
    int rc = aster_os_mkdir("/SD:" COVER_DIR);
    if (rc && rc != -EEXIST)
        return rc;
    aster_os_file_t file = {0};
    char path[80];
    snprintf(path, sizeof(path), "/SD:%s", paths[next]);
    rc = aster_os_file_open(&file, path, ASTER_OS_CREATE | ASTER_OS_WRITE | ASTER_OS_TRUNCATE);
    if (rc)
        return rc;
    int bytes = aster_os_file_write(&file, job->data, job->length);
    rc = aster_os_file_close(&file);
    return bytes == (int)job->length ? rc : -EIO;
}

static void save_worker(void *unused)
{
    (void)unused;
    for (;;) {
        portENTER_CRITICAL(&lock);
        struct thumbnail *job = pending;
        pending = NULL;
        unsigned next = slot ^ 1;
        if (!job)
            writer = false;
        portEXIT_CRITICAL(&lock);
        if (!job)
            break;
        int rc = write_thumbnail(next, job);
        portENTER_CRITICAL(&lock);
        if (!rc && job->epoch == epoch) {
            slot = next;
            saved = epoch;
            revision++;
            if (!revision)
                revision++;
        }
        portEXIT_CRITICAL(&lock);
        ESP_LOGI("bt_cover", "thumbnail %u bytes, storage=%d", (unsigned)job->length, rc);
        free(job);
    }
    vTaskDelete(NULL);
}

static void publish(struct thumbnail *job)
{
    portENTER_CRITICAL(&lock);
    struct thumbnail *obsolete = pending;
    pending = job;
    bool start = !writer;
    writer = true;
    portEXIT_CRITICAL(&lock);
    free(obsolete);
    if (start && xTaskCreate(save_worker, "bt_cover", 4096, NULL, 3, NULL) != pdPASS) {
        portENTER_CRITICAL(&lock);
        pending = NULL;
        writer = false;
        portEXIT_CRITICAL(&lock);
        free(job);
    }
}

static void invalidate(void)
{
    portENTER_CRITICAL(&lock);
    epoch++;
    saved = 0;
    confirm_by = 0;
    portEXIT_CRITICAL(&lock);
    memset(wanted, 0, sizeof(wanted));
}

void aster_bt_cover_reset(void)
{
    invalidate();
    linked = fetching = failed = false;
    free(incoming);
    incoming = NULL;
}

static void fetch(void)
{
    if (!linked || fetching || !wanted[0])
        return;
    incoming = heap_caps_malloc(sizeof(*incoming), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!incoming)
        return;
    incoming->epoch = epoch;
    incoming->length = 0;
    memcpy(active, wanted, HANDLE);
    failed = false;
    fetching = esp_avrc_ct_cover_art_get_linked_thumbnail(active) == ESP_OK;
    if (!fetching) {
        free(incoming);
        incoming = NULL;
    }
}

static void set_handle(const uint8_t *id, int length)
{
    if (!linked)
        return;
    if (!id || length != HANDLE) {
        invalidate();
        return;
    }
    for (unsigned i = 0; i < HANDLE; i++)
        if (id[i] < '0' || id[i] > '9') {
            invalidate();
            return;
        }
    if (!memcmp(wanted, id, HANDLE)) {
        portENTER_CRITICAL(&lock);
        confirm_by = 0;
        bool retry = !saved && !writer;
        portEXIT_CRITICAL(&lock);
        if (retry)
            fetch();
        return;
    }
    invalidate();
    memcpy(wanted, id, HANDLE);
    fetch();
}

static void data_event(const esp_avrc_ct_cb_param_t *p)
{
    if (!fetching)
        return;
    size_t n = p->cover_art_data.data_len;
    if (p->cover_art_data.status != ESP_BT_STATUS_SUCCESS || (n && !p->cover_art_data.p_data) ||
        n > COVER_LIMIT - incoming->length)
        failed = true;
    if (!failed && n) {
        memcpy(incoming->data + incoming->length, p->cover_art_data.p_data, n);
        incoming->length += n;
    }
    if (!p->cover_art_data.final && p->cover_art_data.status == ESP_BT_STATUS_SUCCESS)
        return;
    bool current = incoming->epoch == epoch;
    if (current && !failed && incoming->length >= 4 &&
        incoming->data[0] == 0xff && incoming->data[1] == 0xd8)
        publish(incoming);
    else
        free(incoming);
    incoming = NULL;
    fetching = false;
    if (!current)
        fetch();
}

/* Called serially on Bluedroid's event task. Label 15 is reserved for artwork. */
void aster_bt_cover_event(esp_avrc_ct_cb_event_t event, const esp_avrc_ct_cb_param_t *p)
{
    if (event == ESP_AVRC_CT_CONNECTION_STATE_EVT && !p->conn_stat.connected) {
        aster_bt_cover_reset();
    } else if (event == ESP_AVRC_CT_REMOTE_FEATURES_EVT &&
               (p->rmt_feats.tg_feat_flag & ESP_AVRC_FEAT_FLAG_TG_COVER_ART)) {
        ESP_LOGI("bt_cover", "phone advertises AVRCP cover art");
        esp_avrc_ct_cover_art_connect(ESP_AVRC_CA_MTU_MAX);
    } else if (event == ESP_AVRC_CT_COVER_ART_STATE_EVT) {
        aster_bt_cover_reset();
        linked = p->cover_art_state.state == ESP_AVRC_COVER_ART_CONNECTED;
        if (linked)
            esp_avrc_ct_send_metadata_cmd(15, ESP_AVRC_MD_ATTR_COVER_ART);
    } else if (event == ESP_AVRC_CT_CHANGE_NOTIFY_EVT &&
               p->change_ntf.event_id == ESP_AVRC_RN_TRACK_CHANGE && linked) {
        /* Android can also send TRACK_CHANGE for every lyric line. Keep
         * confirmed artwork during the round trip for an unchanged handle. */
        int64_t deadline = esp_timer_get_time() + 700000;
        portENTER_CRITICAL(&lock);
        confirm_by = deadline;
        portEXIT_CRITICAL(&lock);
    } else if (event == ESP_AVRC_CT_METADATA_RSP_EVT &&
               p->meta_rsp.attr_id == ESP_AVRC_MD_ATTR_COVER_ART) {
        set_handle(p->meta_rsp.attr_text, p->meta_rsp.attr_length);
    } else if (event == ESP_AVRC_CT_COVER_ART_DATA_EVT) {
        data_event(p);
    }
}

void aster_bt_cover_info(aster_player_info_t *info)
{
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&lock);
    if (saved && (!confirm_by || now < confirm_by)) {
        strcpy(info->path, paths[slot]);
        info->flags |= ASTER_PLAYER_F_COVER;
        info->reserved[2] = revision;
    }
    portEXIT_CRITICAL(&lock);
}

uint8_t aster_bt_cover_mask(void)
{
    return atomic_load(&linked) ? ESP_AVRC_MD_ATTR_COVER_ART : 0;
}
#else
void aster_bt_cover_event(esp_avrc_ct_cb_event_t event, const esp_avrc_ct_cb_param_t *p)
{
    (void)event;
    (void)p;
}
void aster_bt_cover_reset(void) {}
uint8_t aster_bt_cover_mask(void) { return 0; }
void aster_bt_cover_info(aster_player_info_t *info) { (void)info; }
#endif

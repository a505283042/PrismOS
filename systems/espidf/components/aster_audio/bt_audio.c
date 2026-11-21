/* SPDX-License-Identifier: MIT */
/* Optional IDF Classic A2DP receiver. No Bluetooth tasks or PCM buffer are
 * created at boot. Stack callbacks only copy data; one worker owns the DAC. */
#include "aster_power.h"
#include "sdkconfig.h"
#if CONFIG_BT_ENABLED && CONFIG_BT_CLASSIC_ENABLED
#include "aster_audio_output.h"
#include "aster_media.h"
#include "aster_platform.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_a2dp_api.h"
#include "esp_a2dp_legacy_api.h"
#include "bt_avrcp.h"
#include "bt_source.h"
#include "esp_console.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/stream_buffer.h"
#include <stdatomic.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#define PCM_CAPACITY 65536u
static const char *TAG = "bt_audio";
static const char *NAME = "Aster-S31 Audio";
static atomic_bool running, stop_requested, accept_pcm, connected, playing, profile_ready,
    profile_down;
static atomic_uint rate, channels, received, dropped, submitted, gaps;
static atomic_int last_error;
static atomic_bool avrcp_enabled, acl_connected;
static atomic_int acl_status, acl_reason, auth_status;
static atomic_uint acl_attempts;
static atomic_bool source_role, scanning, scan_requested, connect_requested, disconnect_requested,
    codec_ready;
static atomic_uint discovery_state;
static atomic_int discovery_error;
static atomic_uint a2dp_state;
static atomic_bool want_connection, connecting;
static aster_player_bt_list_t discovery;
static esp_bd_addr_t target_peer;
static char target_name[62];
static char target_address[18];
static portMUX_TYPE discovery_lock = portMUX_INITIALIZER_UNLOCKED;
static StreamBufferHandle_t pcm_stream;
static StaticStreamBuffer_t pcm_stream_record;
static atomic_bool dirty;
static esp_bd_addr_t peer;
static portMUX_TYPE peer_lock = portMUX_INITIALIZER_UNLOCKED;

/* Only a successful receiver connection identifies a phone. A paired output
 * speaker must never be selected as the input on the next receiver start. */
struct phone_session {
    esp_bd_addr_t last;
    int64_t reconnect_at;
    bool observed;
};

static void load_phone(struct phone_session *session)
{
    nvs_handle_t handle;
    if (nvs_open("asterbt", NVS_READONLY, &handle) != ESP_OK)
        return;
    size_t length = sizeof(session->last);
    bool known = nvs_get_blob(handle, "last_input", session->last, &length) == ESP_OK &&
                 length == sizeof(session->last);
    nvs_close(handle);
    if (known)
        session->reconnect_at = esp_timer_get_time() + 2000000;
}

static void poll_phone(struct phone_session *session)
{
    if (atomic_load(&connected)) {
        session->reconnect_at = 0;
        if (session->observed)
            return;
        session->observed = true;
        esp_bd_addr_t current;
        portENTER_CRITICAL(&peer_lock);
        memcpy(current, peer, sizeof(current));
        portEXIT_CRITICAL(&peer_lock);
        if (!memcmp(session->last, current, sizeof(current)))
            return;
        memcpy(session->last, current, sizeof(current));
        nvs_handle_t handle;
        if (nvs_open("asterbt", NVS_READWRITE, &handle) == ESP_OK) {
            if (nvs_set_blob(handle, "last_input", current, sizeof(current)) == ESP_OK)
                nvs_commit(handle);
            nvs_close(handle);
        }
        return;
    }
    session->observed = false;
    if (!session->reconnect_at || esp_timer_get_time() < session->reconnect_at)
        return;
    session->reconnect_at = 0;
    if (atomic_load(&scanning) || atomic_load(&acl_connected) ||
        atomic_load(&a2dp_state) != ESP_A2D_CONNECTION_STATE_DISCONNECTED)
        return;
    esp_bd_addr_t bonds[16];
    int count = sizeof(bonds) / sizeof(bonds[0]);
    if (esp_bt_gap_get_bond_device_list(&count, bonds) != ESP_OK)
        return;
    for (int i = 0; i < count; i++) {
        if (!memcmp(bonds[i], session->last, sizeof(session->last))) {
            esp_err_t e = esp_a2d_sink_connect(bonds[i]);
            ESP_LOGI(TAG, "reconnect last paired phone: %s", esp_err_to_name(e));
            break;
        }
    }
}

static int bt_error(esp_err_t error)
{
    if (error == ESP_OK)
        return 0;
    ESP_LOGE(TAG, "stack call failed: %s (%d)", esp_err_to_name(error), error);
    return error == ESP_ERR_NO_MEM          ? -ENOMEM
           : error == ESP_ERR_TIMEOUT       ? -ETIMEDOUT
           : error == ESP_ERR_NOT_SUPPORTED ? -ENOTSUP
                                            : -EIO;
}

static void discovered(esp_bt_gap_cb_param_t *p)
{
    uint32_t cod = 0;
    int rssi = -127;
    char name[62] = {0};
    for (int i = 0; i < p->disc_res.num_prop; i++) {
        esp_bt_gap_dev_prop_t *prop = &p->disc_res.prop[i];
        if (!prop->val)
            continue;
        if (prop->type == ESP_BT_GAP_DEV_PROP_COD && prop->len >= 4)
            memcpy(&cod, prop->val, 4);
        else if (prop->type == ESP_BT_GAP_DEV_PROP_RSSI && prop->len >= 1)
            rssi = *(const int8_t *)prop->val;
        else if (prop->type == ESP_BT_GAP_DEV_PROP_BDNAME) {
            size_t n = prop->len;
            if (n >= sizeof(name))
                n = sizeof(name) - 1;
            memcpy(name, prop->val, n);
        } else if (prop->type == ESP_BT_GAP_DEV_PROP_EIR) {
            uint8_t n = 0;
            uint8_t *text =
                esp_bt_gap_resolve_eir_data(prop->val, ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME, &n);
            if (!text)
                text = esp_bt_gap_resolve_eir_data(prop->val, ESP_BT_EIR_TYPE_SHORT_LOCAL_NAME, &n);
            if (text) {
                if (n >= sizeof(name))
                    n = sizeof(name) - 1;
                memcpy(name, text, n);
                name[n] = 0;
            }
        }
    }
    if (cod && !(esp_bt_gap_get_cod_srvc(cod) & ESP_BT_COD_SRVC_RENDERING))
        return;
    char address[18];
    snprintf(address, sizeof(address), "%02x:%02x:%02x:%02x:%02x:%02x", p->disc_res.bda[0],
             p->disc_res.bda[1], p->disc_res.bda[2], p->disc_res.bda[3], p->disc_res.bda[4],
             p->disc_res.bda[5]);
    bool has_name = name[0] != 0;
    if (!has_name)
        memcpy(name, address, sizeof(address));
    portENTER_CRITICAL(&discovery_lock);
    unsigned index = 0;
    for (; index < discovery.count; index++)
        if (!strcmp(discovery.devices[index].address, address))
            break;
    if (index < ASTER_PLAYER_BT_DEVICES_MAX) {
        bool fresh = index == discovery.count;
        if (fresh)
            discovery.count++;
        aster_player_bt_device_t *d = &discovery.devices[index];
        memcpy(d->address, address, sizeof(address));
        /* GAP can report RSSI and name in separate events. Preserve fields missing
   * from later updates instead of replacing a learned name with its address. */
        if (fresh || has_name)
            memcpy(d->name, name, sizeof(name));
        if (fresh || rssi != -127)
            d->rssi = rssi;
    }
    portEXIT_CRITICAL(&discovery_lock);
    ESP_LOGI(TAG, "audio device: %s / %s / %d dBm", address, name, rssi);
}

static void gap_callback(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *p)
{
    if (event == ESP_BT_GAP_DISC_RES_EVT && !atomic_load(&stop_requested)) {
        discovered(p);
        return;
    }
    if (event == ESP_BT_GAP_DISC_STATE_CHANGED_EVT) {
        bool scan = p->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STARTED;
        atomic_store(&scanning, scan);
        if (!scan) {
            unsigned expected = ASTER_PLAYER_BT_SCANNING;
            atomic_compare_exchange_strong(&discovery_state, &expected, ASTER_PLAYER_BT_IDLE);
        }
        return;
    }
    if (event == ESP_BT_GAP_CFM_REQ_EVT)
        esp_bt_gap_ssp_confirm_reply(p->cfm_req.bda, !atomic_load(&stop_requested));
    else if (event == ESP_BT_GAP_PIN_REQ_EVT) {
        esp_bt_pin_code_t pin = {0};
        unsigned length = p->pin_req.min_16_digit ? 16 : 4;
        memset(pin, '0', length);
        esp_bt_gap_pin_reply(p->pin_req.bda, true, length, pin);
    } else if (event == ESP_BT_GAP_AUTH_CMPL_EVT) {
        atomic_store(&auth_status, p->auth_cmpl.stat);
        ESP_LOGI(TAG, "pairing status=%u", p->auth_cmpl.stat);
    } else if (event == ESP_BT_GAP_ACL_CONN_CMPL_STAT_EVT) {
        atomic_fetch_add(&acl_attempts, 1);
        atomic_store(&acl_status, p->acl_conn_cmpl_stat.stat);
        atomic_store(&acl_connected, p->acl_conn_cmpl_stat.stat == ESP_BT_STATUS_SUCCESS);
        const uint8_t *addr = p->acl_conn_cmpl_stat.bda;
        ESP_LOGI(TAG, "ACL connected status=%d handle=%u peer=%02x:%02x:%02x:%02x:%02x:%02x",
                 p->acl_conn_cmpl_stat.stat, p->acl_conn_cmpl_stat.handle,
                 addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
    } else if (event == ESP_BT_GAP_ACL_DISCONN_CMPL_STAT_EVT) {
        atomic_store(&acl_connected, false);
        atomic_store(&acl_reason, p->acl_disconn_cmpl_stat.reason);
        ESP_LOGW(TAG, "ACL disconnected reason=%d handle=%u", p->acl_disconn_cmpl_stat.reason,
                 p->acl_disconn_cmpl_stat.handle);
    }
}

static void a2dp_callback(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *p)
{
    if (event == ESP_A2D_PROF_STATE_EVT) {
        if (p->a2d_prof_stat.init_state == ESP_A2D_INIT_SUCCESS)
            atomic_store(&profile_ready, true);
        else
            atomic_store(&profile_down, true);
    } else if (event == ESP_A2D_CONNECTION_STATE_EVT) {
        bool up = p->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTED;
        if (p->conn_stat.state != ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
            portENTER_CRITICAL(&peer_lock);
            memcpy(peer, p->conn_stat.remote_bda, sizeof(peer));
            portEXIT_CRITICAL(&peer_lock);
        }
        atomic_store(&connected, up);
        if (atomic_load(&source_role))
            atomic_store(&connecting, p->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTING);
        if (up)
            esp_bt_gap_set_scan_mode(ESP_BT_NON_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
        if (p->conn_stat.state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
            atomic_store(&playing, false);
            if (atomic_load(&source_role)) {
                atomic_store(&codec_ready, false);
                aster_bt_tx_link(false, false, atomic_load(&rate));
                atomic_store(&discovery_state, atomic_load(&want_connection)
                                                   ? ASTER_PLAYER_BT_CONNECTING
                                                   : ASTER_PLAYER_BT_IDLE);
            } else if (!atomic_load(&stop_requested))
                esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
        }
        if (up && atomic_load(&source_role)) {
            atomic_store(&want_connection, false);
            atomic_store(&discovery_state, ASTER_PLAYER_BT_CONNECTED);
            aster_bt_tx_link(true, false, atomic_load(&rate));
        }
        atomic_store(&a2dp_state, p->conn_stat.state);
        ESP_LOGI(TAG, "connection state=%d", p->conn_stat.state);
    } else if (event == ESP_A2D_AUDIO_STATE_EVT) {
        atomic_store(&playing, p->audio_stat.state == ESP_A2D_AUDIO_STATE_STARTED);
        if (atomic_load(&source_role))
            aster_bt_tx_link(atomic_load(&connected), atomic_load(&playing), atomic_load(&rate));
        ESP_LOGI(TAG, "stream state=%d", p->audio_stat.state);
    } else if (event == ESP_A2D_REPORT_SNK_CODEC_CAPS_EVT && atomic_load(&source_role)) {
        esp_a2d_mcc_t pref = p->a2d_report_snk_codec_caps_stat.mcc;
        if (pref.type != ESP_A2D_MCT_SBC)
            return;
        esp_a2d_cie_sbc_t *s = &pref.cie.sbc_info;
        /* IDF's internal SBC source advertises 44.1 kHz only (bta_av_co_sbc_caps).
   * Intersect both ends; a sink supporting 48 kHz does not add that local cap. */
        s->samp_freq = s->samp_freq & ESP_A2D_SBC_CIE_SF_44K ? ESP_A2D_SBC_CIE_SF_44K : 0;
        s->ch_mode = s->ch_mode & ESP_A2D_SBC_CIE_CH_MODE_JOINT_STEREO
                         ? ESP_A2D_SBC_CIE_CH_MODE_JOINT_STEREO
                     : s->ch_mode & ESP_A2D_SBC_CIE_CH_MODE_STEREO ? ESP_A2D_SBC_CIE_CH_MODE_STEREO
                                                                   : 0;
        if (!s->samp_freq || !s->ch_mode) {
            atomic_store(&last_error, -ENOTSUP);
            return;
        }
        s->block_len = ESP_A2D_SBC_CIE_BLOCK_LEN_16;
        s->num_subbands = ESP_A2D_SBC_CIE_NUM_SUBBANDS_8;
        s->alloc_mthd = s->alloc_mthd & ESP_A2D_SBC_CIE_ALLOC_MTHD_LOUDNESS
                            ? ESP_A2D_SBC_CIE_ALLOC_MTHD_LOUDNESS
                            : ESP_A2D_SBC_CIE_ALLOC_MTHD_SNR;
        if (s->max_bitpool > 53)
            s->max_bitpool = 53;
        atomic_store(&rate, s->samp_freq == ESP_A2D_SBC_CIE_SF_48K ? 48000 : 44100);
        esp_err_t e =
            esp_a2d_source_set_pref_mcc(p->a2d_report_snk_codec_caps_stat.conn_hdl, &pref);
        if (e)
            atomic_store(&last_error, bt_error(e));
    } else if (event == ESP_A2D_SRC_SET_PREF_MCC_EVT && atomic_load(&source_role)) {
        bool ok = p->a2d_set_pref_mcc_stat.set_status == ESP_BT_STATUS_SUCCESS;
        atomic_store(&codec_ready, ok);
        if (!ok)
            atomic_store(&last_error, -ENOTSUP);
        ESP_LOGI(TAG, "SBC preference accepted=%d status=%d rate=%u", ok,
                 p->a2d_set_pref_mcc_stat.set_status, atomic_load(&rate));
    } else if (event == ESP_A2D_MEDIA_CTRL_ACK_EVT && atomic_load(&source_role)) {
        if (p->media_ctrl_stat.cmd == ESP_A2D_MEDIA_CTRL_CHECK_SRC_RDY &&
            p->media_ctrl_stat.status == ESP_A2D_MEDIA_CTRL_ACK_SUCCESS &&
            atomic_load(&codec_ready) && !atomic_load(&stop_requested))
            esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_START);
    } else if (event == ESP_A2D_AUDIO_CFG_EVT && !atomic_load(&source_role)) {
        if (p->audio_cfg.mcc.type != ESP_A2D_MCT_SBC) {
            atomic_store(&last_error, -ENOTSUP);
            return;
        }
        const esp_a2d_cie_sbc_t *s = &p->audio_cfg.mcc.cie.sbc_info;
        unsigned hz = s->samp_freq & ESP_A2D_SBC_CIE_SF_48K   ? 48000
                      : s->samp_freq & ESP_A2D_SBC_CIE_SF_44K ? 44100
                      : s->samp_freq & ESP_A2D_SBC_CIE_SF_32K ? 32000
                                                              : 16000;
        unsigned ch = s->ch_mode == ESP_A2D_SBC_CIE_CH_MODE_MONO ? 1 : 2;
        atomic_store(&channels, ch);
        atomic_store(&rate, hz);
        ESP_LOGI(TAG, "SBC PCM %u Hz, %u channels / 16 bit", hz, ch);
    }
}

static void pcm_callback(const uint8_t *data, uint32_t len)
{
    if (!atomic_load(&accept_pcm) || !data || !len)
        return;
    unsigned frame = atomic_load(&channels) * 2;
    if (!frame || len % frame || len > PCM_CAPACITY ||
        xStreamBufferSpacesAvailable(pcm_stream) < len) {
        atomic_fetch_add(&dropped, len);
        return;
    }
    size_t n = xStreamBufferSend(pcm_stream, data, len, 0);
    atomic_fetch_add(&received, n);
    if (n != len)
        atomic_fetch_add(&dropped, len - n);
}

static bool wait_flag(atomic_bool *flag, unsigned milliseconds)
{
    int64_t end = esp_timer_get_time() + (int64_t)milliseconds * 1000;
    while (!atomic_load(flag) && esp_timer_get_time() < end)
        vTaskDelay(pdMS_TO_TICKS(10));
    return atomic_load(flag);
}

/* Keep the GAP and A2DP callbacks registered until the last channel and
 * baseband link close. DISCONNECTING is only the start of that exchange. */
static void disconnect_link(bool source)
{
    int64_t deadline = esp_timer_get_time() + 5500000;
    int requested_state = -1;
    for (;;) {
        int state = atomic_load(&a2dp_state);
        if (state == ESP_A2D_CONNECTION_STATE_DISCONNECTED && atomic_load(&connecting))
            state = ESP_A2D_CONNECTION_STATE_CONNECTING;
        if (state == ESP_A2D_CONNECTION_STATE_DISCONNECTED && !atomic_load(&acl_connected))
            return;
        if (state != requested_state && (state == ESP_A2D_CONNECTION_STATE_CONNECTING ||
                                         state == ESP_A2D_CONNECTION_STATE_CONNECTED)) {
            esp_bd_addr_t remote;
            if (source) {
                portENTER_CRITICAL(&discovery_lock);
                memcpy(remote, target_peer, sizeof(remote));
                portEXIT_CRITICAL(&discovery_lock);
                esp_a2d_source_disconnect(remote);
            } else {
                portENTER_CRITICAL(&peer_lock);
                memcpy(remote, peer, sizeof(remote));
                portEXIT_CRITICAL(&peer_lock);
                esp_a2d_sink_disconnect(remote);
            }
            requested_state = state;
        }
        if (esp_timer_get_time() >= deadline) {
            ESP_LOGW(TAG, "disconnect deadline: a2dp=%d acl=%d", state, atomic_load(&acl_connected));
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void discovery_poll(void)
{
    if (!atomic_exchange(&scan_requested, false))
        return;
    portENTER_CRITICAL(&discovery_lock);
    discovery.count = 0;
    discovery.generation++;
    memset(discovery.devices, 0, sizeof(discovery.devices));
    portEXIT_CRITICAL(&discovery_lock);
    esp_err_t e = esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 8, 0);
    if (e != ESP_OK) {
        atomic_store(&discovery_error, bt_error(e));
        atomic_store(&discovery_state, ASTER_PLAYER_BT_ERROR);
    }
}

static int source_poll(int64_t *check_at, int64_t *retry_at, int64_t *connect_deadline,
                       unsigned *attempts)
{
    int64_t now = esp_timer_get_time();
    esp_err_t e;
    if (atomic_exchange(&disconnect_requested, false)) {
        atomic_store(&want_connection, false);
        *attempts = 0;
        disconnect_link(true);
    }
    if (atomic_exchange(&connect_requested, false)) {
        if (atomic_load(&scanning))
            esp_bt_gap_cancel_discovery();
        atomic_store(&want_connection, true);
        *attempts = 0;
        *retry_at = now;
    }
    if (atomic_load(&connected)) {
        *attempts = 0;
        if (!atomic_load(&playing) && atomic_load(&codec_ready) && now >= *check_at) {
            esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_CHECK_SRC_RDY);
            *check_at = now + 500000;
        }
    } else if (atomic_load(&want_connection) && !atomic_load(&scanning)) {
        if (atomic_load(&connecting) && now >= *connect_deadline) {
            disconnect_link(true);
            *retry_at = esp_timer_get_time() + 2000000;
            if (atomic_load(&connecting)) {
                atomic_store(&want_connection, false);
                atomic_store(&discovery_error, -ETIMEDOUT);
                atomic_store(&discovery_state, ASTER_PLAYER_BT_ERROR);
                return 0;
            }
        }
        if (!atomic_load(&connecting) && now >= *retry_at) {
            if (*attempts >= 3) {
                atomic_store(&want_connection, false);
                atomic_store(&discovery_error, -ETIMEDOUT);
                atomic_store(&discovery_state, ASTER_PLAYER_BT_ERROR);
                return 0;
            }
            esp_bd_addr_t remote;
            portENTER_CRITICAL(&discovery_lock);
            memcpy(remote, target_peer, 6);
            portEXIT_CRITICAL(&discovery_lock);
            atomic_store(&connecting, true);
            atomic_store(&discovery_state, ASTER_PLAYER_BT_CONNECTING);
            e = esp_a2d_source_connect(remote);
            (*attempts)++;
            *retry_at = now + 2000000;
            *connect_deadline = now + 25000000;
            if (e) {
                atomic_store(&connecting, false);
                ESP_LOGW(TAG, "source connect attempt %u: %s", *attempts, esp_err_to_name(e));
            }
        }
    }
    return atomic_load(&last_error);
}

static bool speaker_ready(void)
{
    return atomic_load(&source_role) && atomic_load(&codec_ready) && aster_bt_tx_ready() &&
           !atomic_load(&stop_requested);
}

static void receiver(void *ctx)
{
    (void)ctx;
    bool source = atomic_load(&source_role);
    bool controller_init = false, controller_on = false, host_init = false, host_on = false,
         sink = false, output = false, avrcp = false;
    int r = 0;
    uint8_t *storage = heap_caps_malloc(PCM_CAPACITY + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    int16_t *block = heap_caps_malloc(2048, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!storage || !block) {
        r = -ENOMEM;
        goto done;
    }
    pcm_stream = xStreamBufferCreateStatic(PCM_CAPACITY + 1, 1, storage, &pcm_stream_record);
    if (!pcm_stream) {
        r = -ENOMEM;
        goto done;
    }
    if (source)
        aster_bt_tx_prepare(pcm_stream);
    esp_bt_controller_config_t config = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_err_t e = esp_bt_controller_init(&config);
    if (e) {
        r = bt_error(e);
        goto done;
    }
    controller_init = true;
    e = esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT);
    if (e) {
        r = bt_error(e);
        goto done;
    }
    controller_on = true;
    esp_bluedroid_config_t host = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
    e = esp_bluedroid_init_with_cfg(&host);
    if (e) {
        r = bt_error(e);
        goto done;
    }
    host_init = true;
    e = esp_bluedroid_enable();
    if (e) {
        r = bt_error(e);
        goto done;
    }
    host_on = true;
    e = esp_bt_gap_register_callback(gap_callback);
    if (e) {
        r = bt_error(e);
        goto done;
    }
    if (atomic_load(&avrcp_enabled)) {
        avrcp = true;
        e = aster_bt_avrcp_start();
        if (e) {
            r = bt_error(e);
            goto done;
        }
    }
    e = esp_a2d_register_callback(a2dp_callback);
    if (e) {
        r = bt_error(e);
        goto done;
    }
    e = source ? esp_a2d_source_register_data_callback(aster_bt_tx_callback)
               : esp_a2d_sink_register_data_callback(pcm_callback);
    if (e) {
        r = bt_error(e);
        goto done;
    }
    e = source ? esp_a2d_source_init() : esp_a2d_sink_init();
    if (e) {
        r = bt_error(e);
        goto done;
    }
    sink = true;
    if (!wait_flag(&profile_ready, 5000)) {
        r = -ETIMEDOUT;
        goto done;
    }
    esp_bt_io_cap_t io = ESP_BT_IO_CAP_NONE;
    e = esp_bt_gap_set_security_param(ESP_BT_SP_IOCAP_MODE, &io, sizeof(io));
    if (e) {
        r = bt_error(e);
        goto done;
    }
    e = esp_bt_gap_set_device_name(NAME);
    if (e) {
        r = bt_error(e);
        goto done;
    }
    aster_audio_format_t format = {
        sizeof(format), 1, 48000, 2, ASTER_AUDIO_PCM_S16LE, ASTER_AUDIO_OUTPUT_EXCLUSIVE};
    if (!source)
        atomic_store(&accept_pcm, true);
    e = esp_bt_gap_set_scan_mode(source ? ESP_BT_NON_CONNECTABLE : ESP_BT_CONNECTABLE,
                                 source ? ESP_BT_NON_DISCOVERABLE : ESP_BT_GENERAL_DISCOVERABLE);
    if (e) {
        r = bt_error(e);
        goto done;
    }
    ESP_LOGI(TAG, "ready: %s (Classic A2DP %s / SBC)", NAME, source ? "source" : "sink");
    bool buffered = false;
    bool speaker_selected = false;
    struct phone_session phone = {0};
    if (!source)
        load_phone(&phone);
    int64_t check_at = 0, retry_at = 0, connect_deadline = 0;
    unsigned attempts = 0;
    while (!atomic_load(&stop_requested)) {
        discovery_poll();
        if (source) {
            r = source_poll(&check_at, &retry_at, &connect_deadline, &attempts);
            if (r)
                break;
            bool ready = speaker_ready();
            if (ready != speaker_selected) {
                aster_media_set_default_output(ready ? ASTER_AUDIO_OUTPUT_BLUETOOTH
                                                     : ASTER_AUDIO_OUTPUT_WIRED);
                speaker_selected = ready;
            }
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        if (avrcp) {
            aster_bt_avrcp_poll();
            aster_bt_avrcp_refresh_metadata(atomic_load(&playing));
        }
        poll_phone(&phone);
        unsigned hz = atomic_load(&rate), ch = atomic_load(&channels);
        if (atomic_load(&last_error)) {
            r = atomic_load(&last_error);
            break;
        }
        if (!aster_audio_rate_bit(hz)) {
            r = -ENOTSUP;
            break;
        }
        if (!atomic_load(&playing)) {
            if (output) {
                r = aster_audio_output_close(ASTER_AUDIO_OUTPUT_WIRED);
                if (r)
                    break;
                output = false;
            }
            (void)xStreamBufferReceive(pcm_stream, block, 2048, 0);
            buffered = false;
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        if (!output || hz != format.rate) {
            if (output) {
                r = aster_audio_output_close(ASTER_AUDIO_OUTPUT_WIRED);
                if (r)
                    break;
                output = false;
            }
            aster_media_snapshot_t media;
            if (!aster_media_snapshot(&media) &&
                (media.state == ASTER_MEDIA_OPENING || media.state == ASTER_MEDIA_PLAYING ||
                 media.state == ASTER_MEDIA_PAUSED || media.state == ASTER_MEDIA_STOPPING)) {
                aster_media_stop();
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }
            format.rate = hz;
            r = aster_audio_output_open(ASTER_AUDIO_OUTPUT_WIRED, &format);
            if (r)
                break;
            output = true;
            buffered = false;
        }
        size_t queued = xStreamBufferBytesAvailable(pcm_stream);
        if (!buffered && queued < 8192) {
            vTaskDelay(pdMS_TO_TICKS(2));
            continue;
        }
        buffered = true;
        size_t n = xStreamBufferReceive(pcm_stream, block, ch == 1 ? 1024 : 2048, 0);
        if (!n) {
            atomic_fetch_add(&gaps, 1);
            buffered = false;
            vTaskDelay(pdMS_TO_TICKS(2));
            continue;
        }
        unsigned frames = n / (ch * 2);
        if (ch == 1)
            for (int i = (int)frames - 1; i >= 0; i--) {
                block[i * 2] = block[i];
                block[i * 2 + 1] = block[i];
            }
        unsigned sent = 0;
        while (sent < frames && !atomic_load(&stop_requested)) {
            int accepted =
                aster_audio_output_write(ASTER_AUDIO_OUTPUT_WIRED, block + sent * 2, frames - sent);
            if (accepted < 0) {
                r = accepted;
                break;
            }
            sent += (unsigned)accepted;
            atomic_fetch_add(&submitted, (unsigned)accepted);
        }
        if (r)
            break;
    }
done:
    atomic_store(&accept_pcm, false);
    atomic_store(&playing, false);
    atomic_store(&want_connection, false);
    bool media_safe = true;
    if (source) {
        aster_media_set_default_output(ASTER_AUDIO_OUTPUT_WIRED);
        aster_bt_tx_link(false, false, atomic_load(&rate));
        aster_media_stop_output(ASTER_AUDIO_OUTPUT_BLUETOOTH);
        int64_t end = esp_timer_get_time() + 3000000;
        /* An opening decoder may not have opened the endpoint yet. Wait for the
   * whole media session, not just the endpoint's active flag, before freeing
   * the queue that open/write callbacks can still be entering. */
        do {
            aster_media_snapshot_t media;
            media_safe = !aster_media_snapshot(&media) &&
                         (media.endpoint != ASTER_AUDIO_OUTPUT_BLUETOOTH ||
                          media.state == ASTER_MEDIA_IDLE || media.state == ASTER_MEDIA_ENDED ||
                          media.state == ASTER_MEDIA_ERROR) &&
                         !aster_bt_tx_active();
            if (!media_safe)
                vTaskDelay(pdMS_TO_TICKS(10));
        } while (!media_safe && esp_timer_get_time() < end);
    }
    if (host_on)
        esp_bt_gap_set_scan_mode(ESP_BT_NON_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
    if (host_on && atomic_load(&scanning)) {
        esp_bt_gap_cancel_discovery();
        int64_t deadline = esp_timer_get_time() + 1500000;
        while (atomic_load(&scanning) && esp_timer_get_time() < deadline)
            vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (sink)
        disconnect_link(source);
    if (avrcp) {
        esp_err_t e = aster_bt_avrcp_stop();
        if (e && !r)
            r = bt_error(e);
    }
    if (sink) {
        esp_err_t e = source ? esp_a2d_source_deinit() : esp_a2d_sink_deinit();
        if (!e && !wait_flag(&profile_down, 3000) && !r)
            r = -ETIMEDOUT;
    }
    bool safe = true;
    if (host_on && esp_bluedroid_disable() != ESP_OK)
        safe = false;
    if (host_init && safe && esp_bluedroid_deinit() != ESP_OK)
        safe = false;
    if (controller_on && safe && esp_bt_controller_disable() != ESP_OK)
        safe = false;
    if (controller_init && safe && esp_bt_controller_deinit() != ESP_OK)
        safe = false;
    if (output) {
        int closed = aster_audio_output_close(ASTER_AUDIO_OUTPUT_WIRED);
        if (closed)
            atomic_store(&dirty, true);
        if (!r)
            r = closed;
    }
    /* On a stack shutdown failure, retain callback storage until reboot rather
  * than allowing an in-flight callback to access freed memory. */
    if (safe && media_safe) {
        if (source)
            aster_bt_tx_prepare(NULL);
        if (pcm_stream) {
            vStreamBufferDelete(pcm_stream);
            pcm_stream = NULL;
        }
        free(storage);
        free(block);
    } else {
        dirty = true;
        if (!r)
            r = -EIO;
    }
    atomic_store(&connected, false);
    atomic_store(&last_error, r);
    if (source)
        atomic_store(&discovery_state, r ? ASTER_PLAYER_BT_ERROR : ASTER_PLAYER_BT_IDLE);
    ESP_LOGI(TAG, "stopped result=%d retained=%d", r, dirty);
    atomic_store(&running, false);
    aster_power_activity_end();
    vTaskDelete(NULL);
}

static int start_audio_impl(bool enable_avrcp, bool source, bool connect_first)
{
    aster_media_snapshot_t media;
    if (source && !aster_media_snapshot(&media) && media.endpoint == ASTER_AUDIO_OUTPUT_BLUETOOTH &&
        (media.state == ASTER_MEDIA_OPENING || media.state == ASTER_MEDIA_PLAYING ||
         media.state == ASTER_MEDIA_PAUSED || media.state == ASTER_MEDIA_STOPPING)) {
        puts("media session busy");
        return -EBUSY;
    }
    if (dirty) {
        puts("Bluetooth cleanup failed; reboot required");
        return -EBUSY;
    }
    bool expected = false;
    if (!atomic_compare_exchange_strong(&running, &expected, true)) {
        puts("Bluetooth busy");
        return -EBUSY;
    }
    atomic_store(&stop_requested, false);
    atomic_store(&last_error, 0);
    atomic_store(&profile_ready, false);
    atomic_store(&profile_down, false);
    atomic_store(&avrcp_enabled, enable_avrcp);
    atomic_store(&acl_connected, false);
    atomic_store(&acl_attempts, 0);
    atomic_store(&source_role, source);
    atomic_store(&scan_requested, source && !connect_first);
    atomic_store(&connect_requested, connect_first);
    atomic_store(&disconnect_requested, false);
    atomic_store(&want_connection, false);
    atomic_store(&connecting, false);
    atomic_store(&a2dp_state, ESP_A2D_CONNECTION_STATE_DISCONNECTED);
    atomic_store(&codec_ready, false);
    atomic_store(&scanning, false);
    atomic_store(&discovery_error, 0);
    atomic_store(&discovery_state, connect_first ? ASTER_PLAYER_BT_CONNECTING
                                  : source      ? ASTER_PLAYER_BT_SCANNING
                                                : ASTER_PLAYER_BT_IDLE);
    if (!connect_first) {
        portENTER_CRITICAL(&discovery_lock);
        discovery.count = 0;
        discovery.generation = 0;
        target_address[0] = 0;
        portEXIT_CRITICAL(&discovery_lock);
    }
    atomic_store(&acl_status, -1);
    atomic_store(&acl_reason, -1);
    atomic_store(&auth_status, -1);
    atomic_store(&rate, 48000);
    atomic_store(&channels, 2);
    atomic_store(&received, 0);
    atomic_store(&submitted, 0);
    atomic_store(&dropped, 0);
    atomic_store(&gaps, 0);
    if (xTaskCreate(receiver, "bt_audio", 8192, NULL, 6, NULL) != pdPASS) {
        atomic_store(&running, false);
        return -ENOMEM;
    }
    puts("Bluetooth start queued");
    return 0;
}

static int start_audio(bool avrcp, bool source, bool connect_first)
{
    int r = aster_power_activity_begin();
    if (r)
        return r;
    r = start_audio_impl(avrcp, source, connect_first);
    if (r)
        aster_power_activity_end();
    return r;
}

int aster_idf_bt_audio_shutdown(void)
{
    atomic_store(&stop_requested, true);
    int64_t until = esp_timer_get_time() + 7000000;
    while (atomic_load(&running) && esp_timer_get_time() < until) {
        atomic_store(&stop_requested, true);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return atomic_load(&running) ? -ETIMEDOUT : 0;
}

bool aster_idf_bt_audio_busy(void)
{
    return atomic_load(&running);
}

static bool parse_address(const char *text, esp_bd_addr_t out)
{
    if (!text || strlen(text) != 17)
        return false;
    const char *hex = "0123456789abcdef";
    for (unsigned i = 0; i < 6; i++) {
        char upper = text[3 * i], lower = text[3 * i + 1];
        if (upper >= 'A' && upper <= 'F')
            upper += 'a' - 'A';
        if (lower >= 'A' && lower <= 'F')
            lower += 'a' - 'A';
        const char *hi = strchr(hex, upper), *lo = strchr(hex, lower);
        if (!hi || !lo || (i < 5 && text[3 * i + 2] != ':'))
            return false;
        out[i] = (uint8_t)((hi - hex) * 16 + (lo - hex));
    }
    return true;
}

static int command(int argc, char **argv)
{
    if ((argc == 2 && !strcmp(argv[1], "peers")) ||
        (argc == 3 && !strcmp(argv[1], "receive-from"))) {
        if (!atomic_load(&running) || atomic_load(&stop_requested) ||
            !atomic_load(&profile_ready) || atomic_load(&source_role))
            return 1;
        if (argc == 3) {
            esp_bd_addr_t address;
            if (!parse_address(argv[2], address) ||
                atomic_load(&a2dp_state) != ESP_A2D_CONNECTION_STATE_DISCONNECTED)
                return 1;
            /* An explicit console selection may initiate standard SSP pairing
             * with a discoverable phone even when its old bond has expired. */
            esp_err_t e = esp_a2d_sink_connect(address);
            printf("receiver connect request: %s\n", esp_err_to_name(e));
            return e != ESP_OK;
        }
        esp_bd_addr_t peers[16];
        int count = sizeof(peers) / sizeof(peers[0]);
        if (esp_bt_gap_get_bond_device_list(&count, peers) != ESP_OK)
            return 1;
        for (int i = 0; i < count; i++) {
            const uint8_t *a = peers[i];
            char address[18];
            snprintf(address, sizeof(address), "%02x:%02x:%02x:%02x:%02x:%02x",
                     a[0], a[1], a[2], a[3], a[4], a[5]);
            printf("paired %s\n", address);
        }
        return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "connect")) {
        int r = aster_idf_bt_source_request(ASTER_PLAYER_BT_CONNECT, argv[2]);
        printf("connect result=%d\n", r);
        return r ? 1 : 0;
    }
    if (argc == 3 && !strcmp(argv[1], "volume")) {
        char *end;
        long value = strtol(argv[2], &end, 10);
        if (*end || value < 0 || value > 100)
            return 1;
        int r = aster_plat_audio_set_volume((int)value);
        printf("volume=%ld result=%d\n", value, r);
        return r ? 1 : 0;
    }
    if (argc != 2) {
        puts(
            "aster-bt on | peers | receive-from ADDRESS | source | scan | devices | connect ADDRESS | off | status | play | pause | next | previous | volume 0..100");
        return 1;
    }
    if (!strcmp(argv[1], "source") || !strcmp(argv[1], "scan")) {
        int r = aster_idf_bt_source_request(
            !strcmp(argv[1], "source") ? ASTER_PLAYER_SOURCE_ON : ASTER_PLAYER_BT_SCAN, NULL);
        printf("source command result=%d\n", r);
        return r ? 1 : 0;
    }
    if (!strcmp(argv[1], "devices")) {
        aster_player_bt_list_t list = {.size = sizeof(list), .version = 1};
        aster_idf_bt_source_list(&list);
        printf("SOURCE state=%" PRIu32 " rate=%" PRIu32 " queued=%" PRIu32 " error=%" PRId32
               " generation=%" PRIu32 "\n",
               list.state, list.output_rate, list.queued_bytes, list.error, list.generation);
        for (unsigned i = 0; i < list.count; i++) {
            printf("%s | %s | %" PRId32 " dBm\n", list.devices[i].address, list.devices[i].name,
                   list.devices[i].rssi);
        }
        return 0;
    }
    if (!strcmp(argv[1], "play") || !strcmp(argv[1], "pause") || !strcmp(argv[1], "next") ||
        !strcmp(argv[1], "previous")) {
        int r = aster_bt_avrcp_command(argv[1]);
        printf("AVRCP %s result=%d\n", argv[1], r);
        return r ? 1 : 0;
    }
    if (!strcmp(argv[1], "status")) {
        printf(
            "BT running=%d connected=%d playing=%d rate=%u channels=%u codec=SBC rx=%u frames=%u dropped=%u gap_events=%u error=%d volume=%d\n",
            atomic_load(&running), atomic_load(&connected), atomic_load(&playing),
            atomic_load(&rate), atomic_load(&channels), atomic_load(&received),
            atomic_load(&submitted), atomic_load(&dropped), atomic_load(&gaps),
            atomic_load(&last_error), aster_plat_audio_get_volume());
        printf("LINK acl=%d attempts=%u status=%d reason=%d auth=%d avrcp=%d source=%d\n",
               atomic_load(&acl_connected), atomic_load(&acl_attempts), atomic_load(&acl_status),
               atomic_load(&acl_reason), atomic_load(&auth_status), atomic_load(&avrcp_enabled),
               atomic_load(&source_role));
        if (esp_bluedroid_get_status() == ESP_BLUEDROID_STATUS_ENABLED) {
            const uint8_t *bda = esp_bt_dev_get_address();
            if (bda)
                printf("adapter=%02x:%02x:%02x:%02x:%02x:%02x bonds=%d\n", bda[0], bda[1], bda[2],
                       bda[3], bda[4], bda[5], esp_bt_gap_get_bond_device_num());
        }
        aster_bt_avrcp_status();
        return 0;
    }
    if (!strcmp(argv[1], "off")) {
        atomic_store(&stop_requested, true);
        puts("Bluetooth stop requested");
        return 0;
    }
    if (strcmp(argv[1], "on") && strcmp(argv[1], "on-basic"))
        return 1;
    return start_audio(!strcmp(argv[1], "on"), false, false) ? 1 : 0;
}

bool aster_idf_bt_audio_info(aster_player_info_t *info)
{
    if (atomic_load(&source_role))
        return false;
    if (!atomic_load(&running)) {
        if (!atomic_load(&last_error))
            return false;
        info->mode = ASTER_PLAYER_BLUETOOTH;
        info->state = ASTER_PLAYER_ERROR;
        info->error = atomic_load(&last_error);
        return true;
    }
    if (!atomic_load(&connected) && !atomic_load(&stop_requested))
        return false;
    info->mode = ASTER_PLAYER_BLUETOOTH;
    info->state = atomic_load(&stop_requested) ? ASTER_PLAYER_STOPPING
                  : atomic_load(&playing)      ? ASTER_PLAYER_PLAYING
                  : atomic_load(&connected)    ? ASTER_PLAYER_PAUSED
                                               : ASTER_PLAYER_OPENING;
    if (atomic_load(&connected))
        info->flags |= ASTER_PLAYER_F_CONNECTED;
    info->rate = atomic_load(&rate);
    info->channels = atomic_load(&channels);
    info->bits = 16;
    info->error = atomic_load(&last_error);
    memcpy(info->device, NAME, strlen(NAME) + 1);
    if (atomic_load(&avrcp_enabled))
        aster_bt_avrcp_info(info);
    return true;
}

const char *aster_idf_bt_device_name(void)
{
    return atomic_load(&running) ? NAME : "";
}

int aster_idf_bt_audio_control(uint32_t cmd)
{
    if (cmd == ASTER_PLAYER_BT_ON) {
        if (atomic_load(&running))
            return atomic_load(&source_role) ? -EBUSY : 0;
        return start_audio(true, false, false);
    }
    if (cmd == ASTER_PLAYER_BT_OFF) {
        atomic_store(&stop_requested, true);
        if (!atomic_load(&running))
            atomic_store(&last_error, 0);
        return 0;
    }
    const char *name = cmd == ASTER_PLAYER_PAUSE      ? "pause"
                       : cmd == ASTER_PLAYER_RESUME   ? "play"
                       : cmd == ASTER_PLAYER_NEXT     ? "next"
                       : cmd == ASTER_PLAYER_PREVIOUS ? "previous"
                                                      : NULL;
    return name ? aster_bt_avrcp_command(name) : -ENOTSUP;
}

int aster_idf_bt_source_request(uint32_t cmd, const char *address)
{
    if (cmd == ASTER_PLAYER_SOURCE_ON) {
        if (atomic_load(&running))
            return atomic_load(&source_role) ? 0 : -EBUSY;
        return start_audio(false, true, false);
    }
    if (cmd == ASTER_PLAYER_BT_DISCONNECT && !atomic_load(&source_role)) {
        if (!atomic_load(&running) || !atomic_load(&connected))
            return -ENOTCONN;
        esp_bd_addr_t remote;
        portENTER_CRITICAL(&peer_lock);
        memcpy(remote, peer, sizeof(remote));
        portEXIT_CRITICAL(&peer_lock);
        return bt_error(esp_a2d_sink_disconnect(remote));
    }
    bool active = atomic_load(&running);
    bool source = atomic_load(&source_role);
    if (active && atomic_load(&stop_requested))
        return -EBUSY;
    if (cmd == ASTER_PLAYER_BT_SCAN) {
        if (!active)
            return -ENOTCONN;
        if (source && (atomic_load(&connected) || atomic_load(&connecting) ||
                       atomic_load(&want_connection) || atomic_load(&connect_requested)))
            return -EBUSY;
        if (atomic_exchange(&discovery_state, ASTER_PLAYER_BT_SCANNING) != ASTER_PLAYER_BT_SCANNING) {
            atomic_store(&discovery_error, 0);
            atomic_store(&scan_requested, true);
        }
        return 0;
    }
    if (cmd == ASTER_PLAYER_BT_DISCONNECT) {
        if (!active || !source || !atomic_load(&profile_ready))
            return -ENOTCONN;
        aster_media_stop_output(ASTER_AUDIO_OUTPUT_BLUETOOTH);
        atomic_store(&want_connection, false);
        atomic_store(&connect_requested, false);
        atomic_store(&disconnect_requested, true);
        return 0;
    }
    if (cmd != ASTER_PLAYER_BT_CONNECT || !address || strlen(address) != 17)
        return -EINVAL;
    if (active && source && (atomic_load(&connected) || atomic_load(&connecting)))
        return -EBUSY;
    esp_bd_addr_t selected;
    if (!parse_address(address, selected))
        return -EINVAL;
    char canonical[18];
    snprintf(canonical, sizeof(canonical), "%02x:%02x:%02x:%02x:%02x:%02x", selected[0],
             selected[1], selected[2], selected[3], selected[4], selected[5]);
    bool found = false;
    portENTER_CRITICAL(&discovery_lock);
    /* A receiver-to-source handoff can finish after the receiver stops. Only
     * the address selected in the live receiver session is eligible then. */
    bool eligible = active || (!source && !strcmp(canonical, target_address));
    for (unsigned i = 0; i < discovery.count; i++)
        if (eligible && !strcmp(discovery.devices[i].address, canonical)) {
            memcpy(target_peer, selected, 6);
            memcpy(target_name, discovery.devices[i].name, sizeof(target_name));
            memcpy(target_address, canonical, sizeof(canonical));
            found = true;
            break;
        }
    portEXIT_CRITICAL(&discovery_lock);
    if (!found)
        return -ENOENT;
    if (!active)
        return start_audio(false, true, true);
    if (!source)
        return -ENOTCONN;
    atomic_store(&discovery_error, 0);
    atomic_store(&connect_requested, true);
    return 0;
}

int aster_idf_bt_source_list(aster_player_bt_list_t *out)
{
    bool source = atomic_load(&source_role);
    bool active = atomic_load(&running);
    portENTER_CRITICAL(&discovery_lock);
    if (active) {
        out->count = discovery.count;
        out->generation = discovery.generation;
        memcpy(out->devices, discovery.devices, sizeof(out->devices));
        if (source && atomic_load(&connected))
            for (unsigned i = 0; i < out->count; i++)
                if (!strcmp(out->devices[i].address, target_address))
                    out->devices[i].flags = ASTER_PLAYER_F_CONNECTED;
    }
    portEXIT_CRITICAL(&discovery_lock);
    out->state = active || source ? atomic_load(&discovery_state) : ASTER_PLAYER_BT_IDLE;
    if (active && atomic_load(&stop_requested))
        out->state = ASTER_PLAYER_BT_STOPPING;
    out->error = active ? atomic_load(&discovery_error) : source ? atomic_load(&last_error) : 0;
    out->output_rate = active && speaker_ready() ? atomic_load(&rate) : 0;
    out->queued_bytes = aster_bt_tx_queued();
    return 0;
}

void aster_idf_bt_audio_register(void)
{
    const esp_console_cmd_t cmd = {.command = "aster-bt",
                                   .help = "Optional Classic Bluetooth audio receiver",
                                   .func = command};
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
#else
#include <aster/player.h>
#include <stdbool.h>
#include <errno.h>

bool aster_idf_bt_audio_info(aster_player_info_t *info)
{
    (void)info;
    return false;
}

const char *aster_idf_bt_device_name(void)
{
    return "";
}

int aster_idf_bt_audio_control(uint32_t cmd)
{
    (void)cmd;
    return -ENOTSUP;
}

int aster_idf_bt_source_request(uint32_t cmd, const char *address)
{
    (void)cmd;
    (void)address;
    return -ENOTSUP;
}

int aster_idf_bt_source_list(aster_player_bt_list_t *out)
{
    (void)out;
    return -ENOTSUP;
}

void aster_idf_bt_audio_register(void)
{
}

int aster_idf_bt_audio_shutdown(void)
{
    return 0;
}

bool aster_idf_bt_audio_busy(void)
{
    return false;
}
#endif

"""Execute discovery merge against split GAP name/RSSI reports."""
import unittest
from test_waveshare import ROOT,run_c


class Discovery(unittest.TestCase):
    def test_close_waits_for_late_connection_and_acl(self):
        source = (ROOT / 'systems/espidf/components/aster_audio/bt_audio.c').read_text()
        close = source[source.index('static void disconnect_link('):source.index('static void discovery_poll(')]
        run_c(r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdatomic.h>
typedef uint8_t esp_bd_addr_t[6];
enum { ESP_A2D_CONNECTION_STATE_DISCONNECTED, ESP_A2D_CONNECTION_STATE_CONNECTING,
       ESP_A2D_CONNECTION_STATE_CONNECTED, ESP_A2D_CONNECTION_STATE_DISCONNECTING };
static atomic_uint a2dp_state;
static atomic_bool connecting, acl_connected;
static esp_bd_addr_t target_peer = {1}, peer = {2};
static int discovery_lock, peer_lock, calls, elapsed;
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
#define ESP_LOGW(...) ((void)0)
#define pdMS_TO_TICKS(ms) (ms)
static int64_t esp_timer_get_time(void){return elapsed * 1000LL;}
static void esp_a2d_source_disconnect(esp_bd_addr_t addr){assert(addr[0]==1);calls++;}
static void esp_a2d_sink_disconnect(esp_bd_addr_t addr){assert(addr[0]==2);calls++;}
static void vTaskDelay(int ms){
    elapsed+=ms;
    if(elapsed==20){a2dp_state=ESP_A2D_CONNECTION_STATE_CONNECTED;connecting=false;acl_connected=true;}
    if(elapsed==40)a2dp_state=ESP_A2D_CONNECTION_STATE_DISCONNECTING;
    if(elapsed==60)a2dp_state=ESP_A2D_CONNECTION_STATE_DISCONNECTED;
    if(elapsed==90)acl_connected=false;
}
''' + close + r'''
int main(void){
    disconnect_link(true);assert(elapsed==0 && calls==0);
    connecting=true;
    disconnect_link(true);assert(elapsed==90 && calls==2);
    calls=elapsed=0;acl_connected=true;a2dp_state=ESP_A2D_CONNECTION_STATE_CONNECTED;
    disconnect_link(false);assert(elapsed==90 && calls==1);
}
''')

    def test_scan_in_receiver_and_retry_after_gap_error(self):
        source = (ROOT / 'systems/espidf/components/aster_audio/bt_audio.c').read_text()
        poll = source[source.index('static void discovery_poll('):source.index('static int source_poll(')]
        control = source[source.index('int aster_idf_bt_source_request('):source.index('int aster_idf_bt_source_list(')]
        run_c(r'''
#include <aster/player.h>
#include <assert.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
typedef int esp_err_t;
typedef unsigned char esp_bd_addr_t[6];
enum { ESP_BT_INQ_MODE_GENERAL_INQUIRY, ESP_OK = 0 };
static atomic_bool running, source_role, stop_requested, connected, connecting, profile_ready,
                   want_connection, connect_requested, disconnect_requested, scan_requested;
static atomic_uint discovery_state;
static atomic_int discovery_error;
static int discovery_lock, peer_lock, inquiries, starts, gap_result;
static esp_bd_addr_t peer, target_peer;
static char target_name[62], target_address[18];
static aster_player_bt_list_t discovery;
#define portENTER_CRITICAL(lock) ((void)(lock))
#define portEXIT_CRITICAL(lock) ((void)(lock))
static int bt_error(int error) { return error ? -EIO : 0; }
static int esp_bt_gap_start_discovery(int mode, int duration, int count) {
    (void)mode; (void)duration; (void)count;
    inquiries++;
    return gap_result;
}
static int start_audio(bool avrcp, bool source, bool connect_first) {
    assert(!avrcp && source && connect_first);
    starts++;
    return 0;
}
static int esp_a2d_sink_disconnect(esp_bd_addr_t address) { (void)address; return 0; }
enum { ASTER_AUDIO_OUTPUT_BLUETOOTH = 2 };
static void aster_media_stop_output(unsigned endpoint) { (void)endpoint; assert(0); }
''' + poll + control + r'''
int main(void) {
    running = true;
    connected = true; /* A phone connection and the receiver can stay up during inquiry. */
    assert(aster_idf_bt_source_request(ASTER_PLAYER_BT_SCAN, NULL) == 0);
    assert(aster_idf_bt_source_request(ASTER_PLAYER_BT_SCAN, NULL) == 0);
    gap_result = 1;
    discovery_poll();
    assert(inquiries == 1 && discovery_state == ASTER_PLAYER_BT_ERROR && discovery_error == -EIO);
    assert(running && connected && !source_role && !stop_requested);
    gap_result = 0;
    assert(aster_idf_bt_source_request(ASTER_PLAYER_BT_SCAN, NULL) == 0);
    discovery_poll();
    assert(aster_idf_bt_source_request(ASTER_PLAYER_BT_SCAN, NULL) == 0);
    discovery_poll();
    assert(inquiries == 2 && discovery.generation == 2 && !discovery_error && starts == 0);
    discovery.count = 1;
    strcpy(discovery.devices[0].address, "01:02:03:04:05:06");
    assert(aster_idf_bt_source_request(ASTER_PLAYER_BT_CONNECT, discovery.devices[0].address) == -ENOTCONN);
    running = false; connected = false;
    assert(aster_idf_bt_source_request(ASTER_PLAYER_BT_CONNECT, "10:20:30:40:50:60") == -ENOENT);
    assert(aster_idf_bt_source_request(ASTER_PLAYER_BT_CONNECT, target_address) == 0);
    assert(starts == 1 && inquiries == 2);
}
''')

    def test_split_updates_keep_name_signal_and_device_bound(self):
        source=(ROOT/'systems/espidf/components/aster_audio/bt_audio.c').read_text()
        function=source[source.index('static void discovered('):source.index('static void gap_callback(')]
        run_c(r'''
#include <aster/player.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
enum {ESP_BT_GAP_DEV_PROP_COD,ESP_BT_GAP_DEV_PROP_RSSI,ESP_BT_GAP_DEV_PROP_BDNAME,ESP_BT_GAP_DEV_PROP_EIR};
enum {ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME,ESP_BT_EIR_TYPE_SHORT_LOCAL_NAME,ESP_BT_COD_SRVC_RENDERING=64};
typedef struct {int type,len;void *val;} esp_bt_gap_dev_prop_t;
typedef struct {struct {uint8_t bda[6];int num_prop;esp_bt_gap_dev_prop_t *prop;} disc_res;} esp_bt_gap_cb_param_t;
static aster_player_bt_list_t discovery;static int discovery_lock;
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
#define ESP_LOGI(...) ((void)0)
static uint32_t esp_bt_gap_get_cod_srvc(uint32_t cod){return cod;}
static uint8_t *esp_bt_gap_resolve_eir_data(void *eir,int type,uint8_t *length){(void)eir;(void)type;*length=0;return NULL;}
''' + function + r'''
int main(void){
 char name[]="EDIFIER M0";int8_t rssi=-49;
 esp_bt_gap_dev_prop_t props[]={{ESP_BT_GAP_DEV_PROP_BDNAME,sizeof(name)-1,name},{ESP_BT_GAP_DEV_PROP_RSSI,1,&rssi}};
 esp_bt_gap_cb_param_t p={.disc_res={.bda={1,2,3,4,5,6},.num_prop=2,.prop=props}};
 discovered(&p);assert(discovery.count==1 && !strcmp(discovery.devices[0].name,name) && discovery.devices[0].rssi==-49);
 p.disc_res.num_prop=1;p.disc_res.prop=&props[1];rssi=-54;discovered(&p);
 assert(discovery.count==1 && !strcmp(discovery.devices[0].name,name) && discovery.devices[0].rssi==-54);
 p.disc_res.prop=&props[0];discovered(&p);assert(discovery.devices[0].rssi==-54);
 for(unsigned i=2;i<12;i++){p.disc_res.bda[0]=i;discovered(&p);}
 assert(discovery.count==ASTER_PLAYER_BT_DEVICES_MAX);
 return 0;
}
''')

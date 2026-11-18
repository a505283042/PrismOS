"""Run the actual AVRCP owner poll against deterministic transport/volume mocks."""
import unittest
from test_waveshare import ROOT, run_c


class Avrcp(unittest.TestCase):
    def test_key_lifecycle_timeout_and_volume_echo(self):
        source = (ROOT / 'systems/espidf/components/aster_audio/bt_avrcp.c').read_text()
        pairs = source[source.index('static const uint8_t key_pairs'):source.index('static bool ct_initialized')]
        implementation = source[source.index('static void finish_key('):source.index('void aster_bt_avrcp_status(')]
        run_c(r'''
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
typedef int esp_err_t;
enum {ESP_OK=0,ESP_ERR_TIMEOUT=100,ESP_AVRC_RN_VOLUME_CHANGE=13,
 ESP_AVRC_RN_RSP_CHANGED=1,ESP_AVRC_PT_CMD_STATE_PRESSED=0,ESP_AVRC_PT_CMD_STATE_RELEASED=1,
 ESP_AVRC_PT_CMD_PLAY=0x44,ESP_AVRC_PT_CMD_PAUSE=0x46,ESP_AVRC_PT_CMD_FORWARD=0x4b,ESP_AVRC_PT_CMD_BACKWARD=0x4c};
typedef union {uint8_t volume;} esp_avrc_rn_param_t;
static atomic_bool tg_up,ct_up,volume_notify,stopping;
static atomic_int remote_volume,pending_key,key_ack,control_error;
static atomic_uint wire_volume,key_labels,retired_pairs;
static int last_percent,active_key,key_stage,key_pair,next_pair,volume=40,sets,notifications,keys;
#define ESP_LOGW(tag,format,key,phase,labels) ((void)(phase))
static int64_t key_deadline,now;
static int key_code[8],key_state[8],labels[8];
static int aster_plat_audio_get_volume(void){return volume;}
static int aster_plat_audio_set_volume(int value){assert(value>=0 && value<=100);volume=value;sets++;return 0;}
static int64_t esp_timer_get_time(void){return now;}
static void result(esp_err_t e){if(e)atomic_store(&control_error,e);}
static int esp_avrc_tg_send_rn_rsp(int id,int kind,esp_avrc_rn_param_t *p){
 assert(id==ESP_AVRC_RN_VOLUME_CHANGE && kind==ESP_AVRC_RN_RSP_CHANGED && p->volume<=127);notifications++;return 0;
}
static int esp_avrc_ct_send_passthrough_cmd(int label,int code,int state){
 assert(keys<8);labels[keys]=label;key_code[keys]=code;key_state[keys++]=state;return 0;
}
''' + pairs + implementation + r'''
int main(void){
 remote_volume=-1;last_percent=40;
 assert(aster_bt_avrcp_command("play")==-ENOTCONN);
 ct_up=tg_up=true;
 assert(!aster_bt_avrcp_command("next"));
 assert(aster_bt_avrcp_command("pause")==-EBUSY);
 aster_bt_avrcp_poll();assert(keys==1 && key_code[0]==ESP_AVRC_PT_CMD_FORWARD && labels[0]==4 && !key_state[0]);
 aster_bt_avrcp_poll();assert(keys==1);
 key_ack=1;aster_bt_avrcp_poll();assert(keys==2 && labels[1]==5 && key_state[1]==1);
 key_ack=2;aster_bt_avrcp_poll();assert(!pending_key && !key_stage);
 assert(!aster_bt_avrcp_command("pause"));aster_bt_avrcp_poll();
 now=1000001;aster_bt_avrcp_poll();assert(keys==4 && key_state[3]==1 && retired_pairs);
 now=2000002;aster_bt_avrcp_poll();assert(!pending_key && control_error==ESP_ERR_TIMEOUT);
 assert(!aster_bt_avrcp_command("play"));
 aster_bt_avrcp_poll();assert(keys==5 && labels[4]!=labels[2] && labels[4]!=labels[3]);
 key_ack=1;aster_bt_avrcp_poll();assert(keys==6 && key_state[5]==1);
 key_ack=2;aster_bt_avrcp_poll();assert(!pending_key && !key_stage);
 /* Remote updates apply once without echoing a local CHANGED notification. */
 remote_volume=64;volume_notify=true;aster_bt_avrcp_poll();
 assert(volume==50 && sets==1 && wire_volume==64 && notifications==0 && volume_notify);
 aster_bt_avrcp_poll();assert(sets==1 && notifications==0);
 volume=60;aster_bt_avrcp_poll();assert(wire_volume==76 && notifications==1 && !volume_notify);
 volume=61;aster_bt_avrcp_poll();assert(notifications==1);
 /* A disconnected remote cannot change system gain. */
 tg_up=false;remote_volume=127;aster_bt_avrcp_poll();assert(volume==61 && sets==1);
 ct_up=false;aster_bt_avrcp_poll();assert(!pending_key && !key_stage);
 return 0;
}
''')

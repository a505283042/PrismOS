"""Execute discovery merge against split GAP name/RSSI reports."""
import unittest
from test_waveshare import ROOT,run_c


class Discovery(unittest.TestCase):
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

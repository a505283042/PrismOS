"""Optional player contract and sandbox boundary execute actual C code."""
import unittest
from test_waveshare import ROOT, run_c


class Player(unittest.TestCase):
    def test_optional_backend_errors_and_zeroed_snapshot(self):
        source = (ROOT/'common/audio/aster_player_service.c').read_text().replace('#include "aster_player_service.h"', '')
        header = (ROOT/'common/audio/aster_player_service.h').read_text().replace('#pragma once', '')
        run_c(header + source + r'''
#include <assert.h>
static int queries,controls;
static int get(aster_player_info_t *info){queries++;info->capabilities=ASTER_PLAYER_CAP_FLAC;info->error=-EBUSY;return 0;}
static int set(const aster_player_request_t *request){controls++;return request->command==ASTER_PLAYER_PAUSE?-ENOTCONN:0;}
int main(void){
 _Static_assert(sizeof(aster_player_request_t)==272,"request layout");
 _Static_assert(sizeof(aster_player_info_t)==800,"snapshot layout");
 _Static_assert(sizeof(aster_player_bt_list_t)==736,"discovery layout");
 aster_player_info_t out;memset(&out,0x7f,sizeof(out));
 assert(aster_player_service_info(&out)==ASTER_ENOTSUP && !aster_player_service_available());
 aster_player_request_t r={.size=sizeof(r),.version=1,.command=ASTER_PLAYER_BT_ON};
 assert(aster_player_service_control(&r)==ASTER_ENOTSUP);
 const aster_player_ops_t ops={.info=get,.control=set};assert(!aster_player_service_register(&ops));
 aster_player_bt_list_t list;assert(aster_player_service_bt_list(&list)==ASTER_ENOTSUP);
 assert(aster_player_service_register(&ops)==-EBUSY);
 assert(!aster_player_service_info(&out) && out.size==sizeof(out) && out.version==1);
 assert(out.error==ASTER_EBUSY && out.title[0]==0 && out.reserved[3]==0 && queries==1);
 r.command=99;assert(aster_player_service_control(&r)==ASTER_EINVAL && !controls);
 r.command=ASTER_PLAYER_PLAY_FILE;memset(r.path,'x',sizeof(r.path));
 assert(aster_player_service_control(&r)==ASTER_EINVAL && !controls);
 strcpy(r.path,"/SD:/music/a.flac");assert(!aster_player_service_control(&r) && controls==1);
 r.command=ASTER_PLAYER_PAUSE;assert(aster_player_service_control(&r)==ASTER_ESTATE && controls==2);
 r.command=ASTER_PLAYER_BT_CONNECT;memset(r.path,'x',sizeof(r.path));assert(aster_player_service_control(&r)==ASTER_EINVAL && controls==2);
 return 0;
}
''')

    def test_guest_bounds_header_and_file_permission(self):
        source = (ROOT/'common/audio/aster_player_native.inc').read_text().replace('#include "aster_player_service.h"', '')
        run_c(r'''
#include <aster/aster.h>
#include <stdbool.h>
#include <assert.h>
#include <string.h>
#include <errno.h>
typedef void *wasm_exec_env_t;
typedef struct {int permission;} aster_app_ctx_t;
#define ASTER_PERM_FS 1
static unsigned char memory[2048];static aster_app_ctx_t ctx;static unsigned called;
static void *buf(wasm_exec_env_t env,uint32_t off,uint32_t n){(void)env;return off && off<sizeof(memory) && n<=sizeof(memory)-off?memory+off:NULL;}
static aster_app_ctx_t *ctx_of(wasm_exec_env_t env){(void)env;return &ctx;}
static bool aster_ctx_perm(aster_app_ctx_t *c,int permission){return (c->permission&permission)!=0;}
static int sandbox_map(aster_app_ctx_t *c,const char *p,char *out,size_t n){(void)c;(void)n;if(strcmp(p,"/music/test.flac"))return -EPERM;strcpy(out,"/SD:/music/test.flac");return 0;}
static int aster_player_service_info(aster_player_info_t *out){memset(out,0,sizeof(*out));out->size=sizeof(*out);out->version=1;return 0;}
static int aster_player_service_bt_list(aster_player_bt_list_t *list){(void)list;return ASTER_ENOTSUP;}
static int aster_player_service_control(const aster_player_request_t *r){assert(!strcmp(r->path,"/SD:/music/test.flac"));called++;return 0;}
''' + source + r'''
int main(void){
 assert(n_player_get_info(0,0,800)==ASTER_EINVAL);
 assert(n_player_get_info(0,1900,800)==ASTER_EINVAL);
 aster_player_info_t info={.size=sizeof(info),.version=2};memcpy(memory+16,&info,sizeof(info));
 assert(n_player_get_info(0,16,sizeof(info))==ASTER_EINVAL);
 info.version=1;memcpy(memory+16,&info,sizeof(info));assert(!n_player_get_info(0,16,sizeof(info)));
 aster_player_request_t r={.size=sizeof(r),.version=1,.command=ASTER_PLAYER_PLAY_FILE};strcpy(r.path,"/music/test.flac");memcpy(memory+16,&r,sizeof(r));
 assert(n_player_control(0,16,sizeof(r))==ASTER_EPERM && !called);
 ctx.permission=1;assert(!n_player_control(0,16,sizeof(r)) && called==1);
 strcpy(r.path,"/sd/private");memcpy(memory+16,&r,sizeof(r));assert(n_player_control(0,16,sizeof(r))==ASTER_EPERM && called==1);
 memset(r.path,'x',sizeof(r.path));memcpy(memory+16,&r,sizeof(r));assert(n_player_control(0,16,sizeof(r))==ASTER_EINVAL);
 assert(n_player_control(0,1900,sizeof(r))==ASTER_EINVAL);
 assert(n_player_bt_list(0,1900,sizeof(aster_player_bt_list_t))==ASTER_EINVAL);
 aster_player_bt_list_t list={.size=sizeof(list),.version=1};memcpy(memory+16,&list,sizeof(list));
 assert(n_player_bt_list(0,16,sizeof(list))==ASTER_ENOTSUP);
 return 0;
}
''')

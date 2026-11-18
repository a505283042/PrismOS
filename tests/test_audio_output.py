"""Exercise the compiled optional output contract and bit-exact PCM conversion."""
import unittest
from test_waveshare import ROOT, run_c


class AudioOutput(unittest.TestCase):
    def test_native_session_blocks_legacy_teardown(self):
        source = (ROOT / 'boards/fakepod_s31/idf/port/audio.c').read_text(encoding='utf-8')
        wrappers = source[source.index('/* Legacy entry points'):source.index('static int native_caps(')]
        run_c(r'''
#include <stdbool.h>
#include <stddef.h>
#include <assert.h>
#include <errno.h>
struct device {int unused;};
enum i2s_dir {TX=1};
struct i2s_config {unsigned frame_clk_freq;};
static bool native_owner,locked;
static unsigned calls,lock_calls;
static bool lock_hardware(void){lock_calls++;assert(!locked);locked=true;return true;}
static int lock_legacy_hardware(void){return lock_hardware()?0:-EBUSY;}
static void unlock_hardware(void){assert(locked);locked=false;}
static int legacy_configure_locked(const struct device *d,enum i2s_dir dir,const struct i2s_config *c){
 (void)d;(void)dir;(void)c;calls++;return 0;
}
static int legacy_trigger_locked(const struct device *d,enum i2s_dir dir,int trigger){
 (void)d;(void)dir;(void)trigger;calls++;return 0;
}
static int legacy_write_locked(const struct device *d,void *block,size_t size){
 (void)d;(void)block;(void)size;calls++;return 0;
}
static int legacy_read_locked(const struct device *d,void **block,size_t *size){
 (void)d;(void)block;(void)size;calls++;return 0;
}
''' + wrappers + r'''
int main(void){
 struct i2s_config shutdown={0};native_owner=true;
 assert(i2s_configure(NULL,TX,&shutdown)==-EBUSY);
 assert(i2s_trigger(NULL,TX,0)==-EBUSY);
 assert(i2s_write(NULL,NULL,0)==-EBUSY);
 assert(i2s_read(NULL,NULL,NULL)==-EBUSY);
 assert(!calls && !locked && !lock_calls);
 native_owner=false;assert(!i2s_configure(NULL,TX,&shutdown));assert(calls==1 && !locked);
 assert(i2s_configure(NULL,TX,NULL)==-EINVAL && !locked);
 return 0;
}
''')

    def test_legacy_lock_has_bounded_race_wait(self):
        source = (ROOT / 'boards/fakepod_s31/idf/port/audio.c').read_text(encoding='utf-8')
        locking = source[source.index('static atomic_uint volume_pending;'):source.index('static int error(')]
        run_c(r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <errno.h>
#define pdTRUE 1
#define portMAX_DELAY UINT32_MAX
#define pdMS_TO_TICKS(n) (n)
#define ESP_LOGW(...) ((void)0)
static int hardware_lock=1,grant=1;
static bool ready=true,flip_owner;
static atomic_bool native_owner;
static unsigned native_errors,takes,gives,last_wait,applied;
static int xSemaphoreTake(int lock,uint32_t ticks){assert(lock==1);takes++;last_wait=ticks;if(flip_owner)atomic_store(&native_owner,true);return grant;}
static void xSemaphoreGive(int lock){assert(lock==1);gives++;}
''' + locking + r'''
static int codec_volume_locked(uint8_t percent){applied=percent;return 0;}
int main(void){
 atomic_store(&volume_pending,61);assert(!lock_legacy_hardware());assert(last_wait==5 && applied==60);unlock_hardware();
 unsigned calls=takes;native_owner=true;assert(lock_legacy_hardware()==-EBUSY && takes==calls);
 native_owner=false;grant=0;assert(lock_legacy_hardware()==-EBUSY && last_wait==5);
 grant=1;flip_owner=true;calls=gives;assert(lock_legacy_hardware()==-EBUSY && gives==calls+1);
 native_owner=false;flip_owner=false;assert(lock_hardware() && last_wait==portMAX_DELAY);unlock_hardware();
 assert(!native_errors);return 0;
}
''')

    def test_volume_request_never_waits_for_dma(self):
        source = (ROOT / 'boards/fakepod_s31/idf/port/audio.c').read_text(encoding='utf-8')
        setter = source[source.index('int aster_idf_codec_volume('):source.index('int aster_idf_codec_init(')]
        run_c(r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <errno.h>
static bool ready;
static atomic_uint volume_pending;
''' + setter + r'''
int main(void){
 assert(aster_idf_codec_volume(60)==-ENODEV && !atomic_load(&volume_pending));
 ready=true;
 assert(!aster_idf_codec_volume(60) && atomic_load(&volume_pending)==61);
 assert(!aster_idf_codec_volume(42) && atomic_load(&volume_pending)==43);
 assert(!aster_idf_codec_volume(0) && atomic_load(&volume_pending)==1);
 return 0;
}
''')

    def test_contract_and_pcm(self):
        header = (ROOT / 'common/audio/aster_audio_output.h').read_text(encoding='utf-8')
        source = (ROOT / 'common/audio/aster_audio_output.c').read_text(encoding='utf-8')
        source = source.replace('#include "aster_audio_output.h"', '').replace('#include "aster_power.h"', '#include "'+str(ROOT/'common/port/aster_power.c')+'"')
        run_c(header + source + r'''
#include <assert.h>
static int close_error, write_result=1, open_calls;
static int caps(void *ctx,aster_audio_output_caps_t *out) {
 assert(ctx==(void *)42);
 out->formats=ASTER_AUDIO_PCM_BIT(ASTER_AUDIO_PCM_S24LE);
 out->rates=ASTER_AUDIO_RATE_48000;out->channels=ASTER_AUDIO_CHANNEL_BIT(2);
 out->flags=ASTER_AUDIO_OUTPUT_EXCLUSIVE;out->max_write_frames=128;return 0;
}
static int open_output(void *ctx,const aster_audio_format_t *f) {
 (void)ctx;(void)f;open_calls++;return 0;
}
static int write_output(void *ctx,const void *pcm,uint32_t frames) {
 (void)ctx;(void)pcm;(void)frames;return write_result;
}
static int close_output(void *ctx) {(void)ctx;return close_error;}
int main(void) {
 aster_audio_format_t f={sizeof(f),1,48000,2,ASTER_AUDIO_PCM_S24LE,1};
 uint8_t pcm[]={0,0,0,1,0,0,0xff,0xff,0x7f,0,0,0x80,0xff,0xff,0xff,0x56,0x34,0x12};
 int32_t out[6];
 assert(!aster_audio_pcm_to_s32(pcm,3,&f,out,6));
 assert(out[0]==0 && out[1]==256 && out[2]==2147483392 && out[3]==INT32_MIN);
 assert(out[4]==-256 && out[5]==0x12345600);
 assert(aster_audio_pcm_to_s32(pcm,3,&f,out,5)==-EINVAL);
 f.format=ASTER_AUDIO_PCM_S16LE;
 uint8_t pcm16[]={0,0x80,0xff,0x7f};
 assert(!aster_audio_pcm_to_s32(pcm16,1,&f,out,2));
 assert(out[0]==INT32_MIN && out[1]==2147418112);
 f.format=ASTER_AUDIO_PCM_S32LE;
 uint8_t pcm32[]={1,0,0,0,0xff,0xff,0xff,0xff};
 assert(!aster_audio_pcm_to_s32(pcm32,1,&f,out,2));assert(out[0]==1 && out[1]==-1);
 f.format=ASTER_AUDIO_PCM_S24LE;
 aster_audio_output_caps_t cap={.size=123};
 assert(aster_audio_output_caps(1,&cap)==-ENOTSUP && cap.size==123);
 assert(aster_audio_output_open(1,&f)==-ENOTSUP);
 aster_audio_output_ops_t base={.size=sizeof(base),.version=1,.name="mock",
  .caps=caps,.open=open_output,.write=write_output,.close=close_output};
 aster_audio_output_ops_t overrides={.size=sizeof(overrides),.version=1,.name="derived"},derived;
 assert(!aster_audio_output_derive(&derived,&base,&overrides));
 assert(derived.open==base.open && !strcmp(derived.name,"derived"));
 assert(!aster_audio_output_bind(1,&derived,(void *)42));
 assert(aster_audio_output_bind(1,&derived,NULL)==-EEXIST);
 assert(!aster_audio_output_caps(1,&cap) && cap.max_write_frames==128);
 f.rate=96000;assert(aster_audio_output_open(1,&f)==-ENOTSUP && !open_calls);f.rate=48000;
 assert(!aster_audio_output_open(1,&f) && open_calls==1);
 assert(aster_audio_output_open(1,&f)==-EBUSY);
 assert(aster_audio_output_write(1,pcm,2)==1);
 assert(aster_audio_output_write(1,pcm,129)==-EMSGSIZE);
 write_result=0;assert(aster_audio_output_write(1,pcm,2)==-EAGAIN);
 write_result=3;assert(aster_audio_output_write(1,pcm,2)==-EIO);
 write_result=-EIO;assert(aster_audio_output_write(1,pcm,2)==-EIO);
 assert(aster_audio_output_drain(1)==-ENOTSUP);
 close_error=-EIO;assert(aster_audio_output_close(1)==-EIO);
 assert(aster_audio_output_open(1,&f)==-EBUSY);
 close_error=0;assert(!aster_audio_output_close(1));assert(!aster_audio_output_close(1));
 assert(aster_audio_output_write(1,pcm,1)==-EBADF);
 aster_audio_output_seal();assert(aster_audio_output_bind(2,&base,NULL)==-EBUSY);
 return 0;
}
''')


if __name__ == '__main__':
    unittest.main()

"""Check async media control, EOF/error cleanup and task reclamation."""
import subprocess
import tempfile
import unittest
import wave
from pathlib import Path
from test_waveshare import ROOT
from test_audio_decoder import STUB

COMMON=STUB[:STUB.index('static bool abort_read;')]
SESSION=r"""
#include "aster_media.h"
#include "aster_audio_output.h"
#include <pthread.h>
#include <stdatomic.h>
#include <time.h>
static void delay_ms(unsigned ms){struct timespec t={ms/1000,(ms%1000)*1000000};nanosleep(&t,NULL);}
void aster_os_sleep_ms(uint32_t ms){delay_ms(ms);}
int aster_os_mutex_create(aster_os_mutex_t *m){m->handle=malloc(sizeof(pthread_mutex_t));return pthread_mutex_init(m->handle,NULL);}
int aster_os_mutex_lock(aster_os_mutex_t *m,uint32_t ms){(void)ms;return pthread_mutex_lock(m->handle);}
int aster_os_mutex_unlock(aster_os_mutex_t *m){return pthread_mutex_unlock(m->handle);}
struct task {pthread_t thread;aster_os_task_fn entry;void *ctx;atomic_bool done;};
static void *thread_run(void *arg){struct task *t=arg;t->entry(t->ctx);atomic_store(&t->done,true);return NULL;}
int aster_os_task_start(aster_os_task_t *out,const aster_os_task_config_t *cfg,aster_os_task_fn entry,void *ctx){
 (void)cfg;struct task *t=calloc(1,sizeof(*t));assert(t);t->entry=entry;t->ctx=ctx;
 int r=pthread_create(&t->thread,NULL,thread_run,t);assert(!r);out->handle=t;return 0;
}
int aster_os_task_join(aster_os_task_t *out,uint32_t ms){
 (void)ms;struct task *t=out->handle;if(!atomic_load(&t->done))return -EAGAIN;
 assert(!pthread_join(t->thread,NULL));free(t);out->handle=NULL;return 0;
}
static int caps(void *ctx,aster_audio_output_caps_t *c){
 (void)ctx;c->formats=ASTER_AUDIO_PCM_BIT(ASTER_AUDIO_PCM_S32LE);c->rates=ASTER_AUDIO_RATE_48000;
 c->channels=ASTER_AUDIO_CHANNEL_BIT(2);c->flags=ASTER_AUDIO_OUTPUT_EXCLUSIVE;c->max_write_frames=512;return 0;
}
static bool opened;
static int open_output(void *ctx,const aster_audio_format_t *f){(void)ctx;assert(!opened && f->rate==48000);opened=true;return 0;}
static int write_output(void *ctx,const void *pcm,uint32_t n){(void)ctx;(void)pcm;assert(opened);delay_ms(10);return (int)n;}
static int drain_output(void *ctx){(void)ctx;assert(opened);return 0;}
static int close_output(void *ctx){(void)ctx;assert(opened);opened=false;return 0;}
static aster_media_snapshot_t wait_state(uint32_t state){
 aster_media_snapshot_t s;
 for(unsigned i=0;i<300;i++){assert(!aster_media_snapshot(&s));if(s.state==state)return s;delay_ms(2);}
 fprintf(stderr,"state %u expected %u error %d\n",s.state,state,s.error);abort();
}
int main(int argc,char **argv){
 assert(argc==2);
 assert(aster_media_play(argv[1],1)==-ENOTSUP);
 static const aster_audio_output_ops_t ops={.size=sizeof(ops),.version=1,.name="host",
 .caps=caps,.open=open_output,.write=write_output,.drain=drain_output,.close=close_output};
 assert(!aster_audio_output_bind(1,&ops,NULL));assert(!aster_media_init());
 assert(!aster_media_play(argv[1],1));assert(aster_media_play(argv[1],1)==-EBUSY);
 wait_state(ASTER_MEDIA_PLAYING);assert(!aster_media_pause());
 aster_media_snapshot_t paused=wait_state(ASTER_MEDIA_PAUSED),s;
 delay_ms(25);assert(!aster_media_snapshot(&s));assert(s.frame==paused.frame && !opened);
 assert(!aster_media_seek(100));delay_ms(25);assert(!aster_media_snapshot(&s));assert(s.frame==4800);
 assert(!aster_media_resume());wait_state(ASTER_MEDIA_PLAYING);
 wait_state(ASTER_MEDIA_ENDED);delay_ms(5);assert(!aster_media_snapshot(&s));assert(!allocations && !opened && s.frame==12000 && !s.decoder_bytes);
 assert(aster_media_pause()==-EBADF);
 assert(!aster_media_seek(100));
 s=wait_state(ASTER_MEDIA_PLAYING);assert(s.frame>=4800);
 wait_state(ASTER_MEDIA_ENDED);delay_ms(5);assert(!aster_media_snapshot(&s));
 assert(s.frame==12000 && !allocations && !opened);

 assert(!aster_media_play(argv[1],1));wait_state(ASTER_MEDIA_PLAYING);assert(!aster_media_stop());
 wait_state(ASTER_MEDIA_IDLE);delay_ms(5);assert(!aster_media_snapshot(&s));assert(!allocations && !opened);
 assert(!aster_media_play("/nonexistent-aster-test-file",1));wait_state(ASTER_MEDIA_ERROR);
 delay_ms(5);assert(!aster_media_snapshot(&s));assert(s.error==-ENOENT && !allocations && !opened);
 assert(!aster_media_play(argv[1],1));wait_state(ASTER_MEDIA_PLAYING);assert(!aster_media_stop());
 wait_state(ASTER_MEDIA_IDLE);delay_ms(5);assert(!aster_media_snapshot(&s));assert(!allocations);
 pthread_mutex_destroy(lock.handle);
 return 0;
}
"""
# 'lock' is private to the actual service, so retain only the one boot mutex.
SESSION=SESSION.replace(' pthread_mutex_destroy(lock.handle);','')

class AudioMedia(unittest.TestCase):
    def test_lifecycle(self):
        with tempfile.TemporaryDirectory() as td:
            p=Path(td);(p/'test.c').write_text(COMMON+SESSION)
            wav=p/'test.wav'
            with wave.open(str(wav),'wb') as w:
                w.setnchannels(2);w.setsampwidth(3);w.setframerate(48000);w.writeframes(bytes(12000*6))
            binary=p/'test'
            command=['cc','-std=c11','-D_POSIX_C_SOURCE=200809L','-O1','-g','-Wall','-Wextra','-Werror',
              '-Wno-unused-function','-Wno-misleading-indentation',
              '-I'+str(ROOT/'common/audio'),'-I'+str(ROOT/'common/port'),'-I'+str(ROOT/'contract/include'),
              '-I'+str(ROOT/'third_party/dr_libs'),str(p/'test.c')]
            command += [str(ROOT/'common/audio'/name) for name in ('aster_audio_decoder.c','aster_audio_output.c','aster_media.c')]
            command += ['-pthread','-lm','-o',str(binary)]
            compiled=subprocess.run(command,capture_output=True,text=True)
            self.assertEqual(compiled.returncode,0,compiled.stderr)
            subprocess.run([str(binary),str(wav)],check=True,timeout=10)

if __name__=='__main__':unittest.main()

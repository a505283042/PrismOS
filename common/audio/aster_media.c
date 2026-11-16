/* SPDX-License-Identifier: MIT */
#include "aster_media.h"
#include "aster_audio_decoder.h"
#include "aster_audio_output.h"
#include "aster_os.h"
#include <string.h>
#include <errno.h>
#include <stdatomic.h>
static aster_os_mutex_t lock;
static aster_os_task_t worker;
static aster_media_snapshot_t status;
static bool active,paused;
static uint64_t seek_ms=UINT64_MAX;
static atomic_bool stopping;
static void take(void){(void)aster_os_mutex_lock(&lock,ASTER_OS_WAIT_FOREVER);}
static void give(void){(void)aster_os_mutex_unlock(&lock);}
static bool cancelled(void *ctx){(void)ctx;return atomic_load(&stopping);}
int aster_media_init(void){
 if(lock.handle)return 0;
 aster_audio_output_caps_t caps;int r=aster_audio_output_caps(ASTER_AUDIO_OUTPUT_WIRED,&caps);
 if(r)return r;
 r=aster_os_mutex_create(&lock);
 if(!r)status=(aster_media_snapshot_t){.size=sizeof(status),.version=1};
 return r;
}
static void media_worker(void *ctx){
 (void)ctx;
 aster_audio_decoder_t *decoder=NULL;int32_t *pcm=NULL;bool opened=false;
 aster_audio_format_t format={.size=sizeof(format),.version=1,.channels=2,.format=ASTER_AUDIO_PCM_S32LE,.flags=ASTER_AUDIO_OUTPUT_EXCLUSIVE};
 aster_audio_decoder_info_t info={0};uint64_t position=0;
 /* path/endpoint cannot be replaced until active is cleared. */
 int r=aster_audio_decoder_open(status.path,cancelled,NULL,&decoder);
 if(!r){
  aster_audio_decoder_info(decoder,&info);format.rate=info.rate;
  pcm=aster_os_alloc_external(512*2*sizeof(*pcm));if(!pcm)r=-ENOMEM;
 }
 if(!r){
  take();status.rate=info.rate;status.channels=info.channels;status.bits=info.bits;
  status.codec=info.codec;status.total_frames=info.frames;status.decoder_bytes=info.allocated;
  status.decoder_peak=info.peak_allocated;give();
 }
 bool ended=false;
 while(!r && !atomic_load(&stopping)){
  take();bool pause=paused;uint64_t seek=seek_ms;seek_ms=UINT64_MAX;give();
  if(seek!=UINT64_MAX){
   if(opened){r=aster_audio_output_close(status.endpoint);opened=false;if(r)break;}
   uint64_t frame=seek*info.rate/1000;
   if(info.frames && frame>info.frames)frame=info.frames;
   r=aster_audio_decoder_seek(decoder,frame);if(r)break;
   position=frame;take();status.frame=position;give();
  }
  if(pause){
   if(opened){r=aster_audio_output_drain(status.endpoint);int closed=aster_audio_output_close(status.endpoint);opened=false;if(!r)r=closed;if(r)break;}
   take();if(status.state!=ASTER_MEDIA_STOPPING)status.state=ASTER_MEDIA_PAUSED;give();aster_os_sleep_ms(10);continue;
  }
  if(!opened){r=aster_audio_output_open(status.endpoint,&format);if(r)break;opened=true;}
  take();if(status.state!=ASTER_MEDIA_STOPPING)status.state=ASTER_MEDIA_PLAYING;give();
  int n=aster_audio_decoder_read(decoder,pcm,512);
  if(n<0){r=n;break;}
  if(!n){ended=true;break;}
  if(info.channels==1)for(int i=n-1;i>=0;i--){pcm[i*2]=pcm[i];pcm[i*2+1]=pcm[i];}
  unsigned sent=0;
  while(sent<(unsigned)n && !atomic_load(&stopping)){
   int accepted=aster_audio_output_write(status.endpoint,pcm+sent*2,(unsigned)n-sent);
   if(accepted==-EAGAIN){aster_os_sleep_ms(1);continue;}
   if(accepted<0){r=accepted;break;}sent+=(unsigned)accepted;
  }
  position+=sent;aster_audio_decoder_info(decoder,&info);
  take();status.frame=position;status.decoder_bytes=info.allocated;status.decoder_peak=info.peak_allocated;give();
 }
 if(opened){
  if(ended && !r){int drained=aster_audio_output_drain(status.endpoint);if(drained)r=drained;}
  int closed=aster_audio_output_close(status.endpoint);if(!r)r=closed;
 }
 int closed=aster_audio_decoder_close(decoder);if(!r)r=closed;
 aster_os_free_external(pcm);
 take();
 if(atomic_load(&stopping) && (r==0 || r==-ECANCELED || r==-ENOTCONN)){status.state=ASTER_MEDIA_IDLE;status.error=0;}
 else {status.state=r?ASTER_MEDIA_ERROR:ASTER_MEDIA_ENDED;status.error=r;}
 status.decoder_bytes=0;active=false;give();
}
/* Caller holds the session lock; new work is published in one operation. */
static int start_locked(const char *path,uint32_t endpoint,uint64_t initial_seek){
 if(active)return -EBUSY;
 int r=0;
 if(worker.handle){r=aster_os_task_join(&worker,0);if(r)return -EBUSY;}
 status=(aster_media_snapshot_t){.size=sizeof(status),.version=1,.state=ASTER_MEDIA_OPENING,.endpoint=endpoint};
 memcpy(status.path,path,strlen(path)+1);paused=false;seek_ms=initial_seek;atomic_store(&stopping,false);active=true;
 const aster_os_task_config_t config={.name="aster_media",.stack_bytes=12288,.priority=18,.core=-1};
 r=aster_os_task_start(&worker,&config,media_worker,NULL);
 if(r){active=false;status.state=ASTER_MEDIA_ERROR;status.error=r;}
 return r;
}
int aster_media_play(const char *path,uint32_t endpoint){
 if(!lock.handle)return -ENOTSUP;
 if(!path || !*path || strlen(path)>=ASTER_MEDIA_PATH_MAX)return -EINVAL;
 aster_audio_output_caps_t caps;int r=aster_audio_output_caps(endpoint,&caps);if(r)return r;
 take();r=start_locked(path,endpoint,UINT64_MAX);give();return r;
}
int aster_media_snapshot(aster_media_snapshot_t *out){
 if(!out)return -EINVAL;
 if(!lock.handle)return -ENOTSUP;
 take();if(!active && worker.handle)(void)aster_os_task_join(&worker,0);
 *out=status;give();return 0;
}
int aster_media_stop(void){
 if(!lock.handle)return -ENOTSUP;
 take();if(active){atomic_store(&stopping,true);status.state=ASTER_MEDIA_STOPPING;}give();return 0;
}
static int set_pause(bool pause){
 if(!lock.handle)return -ENOTSUP;
 take();int r=active && !atomic_load(&stopping)?0:-EBADF;if(!r)paused=pause;give();return r;
}
int aster_media_pause(void){return set_pause(true);}
int aster_media_resume(void){return set_pause(false);}
int aster_media_seek(uint32_t milliseconds){
 if(!lock.handle)return -ENOTSUP;
 take();int r=-EBADF;
 if(active && status.rate && !atomic_load(&stopping)){seek_ms=milliseconds;r=0;}
 else if(!active && status.state==ASTER_MEDIA_ENDED && status.path[0]){
  /* The system session retains its resolved path/endpoint. Reopen at the
   * requested position without briefly starting from zero before a second
   * seek request; the UI caller never performs decoder/SD work here. */
  char path[ASTER_MEDIA_PATH_MAX];memcpy(path,status.path,sizeof(path));
  uint32_t endpoint=status.endpoint;r=start_locked(path,endpoint,milliseconds);
 }
 give();return r;
}

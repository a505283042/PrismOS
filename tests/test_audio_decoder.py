"""Decode real WAV/FLAC samples through the production OS callback adapter."""
import math
import shutil
import struct
import subprocess
import tempfile
import unittest
import wave
from pathlib import Path
from test_waveshare import ROOT

STUB = r"""
#include "aster_audio_decoder.h"
#include "aster_os.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
static size_t allocations;
void *aster_os_alloc(size_t n,unsigned region){(void)region;void *p=malloc(n);if(p)allocations++;return p;}
void aster_os_free(void *p,unsigned region){(void)region;if(p){assert(allocations);allocations--;free(p);}}
int aster_os_stat(const char *path,aster_os_entry_t *out){FILE *f=fopen(path,"rb");if(!f)return -ENOENT;fseek(f,0,SEEK_END);out->size=ftell(f);out->type=ASTER_OS_FILE;fclose(f);return 0;}
int aster_os_file_open(aster_os_file_t *f,const char *path,unsigned flags){(void)flags;f->handle=fopen(path,"rb");return f->handle?0:-ENOENT;}
int aster_os_file_close(aster_os_file_t *f){int r=fclose(f->handle);f->handle=NULL;return r?-EIO:0;}
int aster_os_file_read(aster_os_file_t *f,void *p,size_t n){size_t got=fread(p,1,n,f->handle);return ferror(f->handle)?-EIO:(int)got;}
int aster_os_file_seek(aster_os_file_t *f,int64_t n,unsigned mode){return fseek(f->handle,(long)n,mode)?-EIO:0;}
static bool abort_read;
static bool cancel(void *ctx){(void)ctx;return abort_read;}
int main(int argc,char **argv){
 assert(argc==4);int bits=atoi(argv[2]);aster_audio_decoder_t *d=NULL;
 assert(!aster_audio_decoder_open(argv[1],cancel,NULL,&d));
 aster_audio_decoder_info_t info;aster_audio_decoder_info(d,&info);
 assert(info.rate==48000 && info.bits==(unsigned)bits && info.channels==2 && info.frames==1024);
 assert(info.allocated>0 && info.peak_allocated<=768*1024);
 int32_t pcm[1024];uint64_t pos=0;
 while(1){
  int n=aster_audio_decoder_read(d,pcm,127);assert(n>=0);if(!n)break;
  for(int i=0;i<n*2;i++){
   uint32_t sample=(uint32_t)((pos*2+i)*37+1)&(UINT32_MAX>>(32-bits));
   uint32_t expected=sample<<(32-bits);uint32_t actual;memcpy(&actual,pcm+i,4);assert(actual==expected);
  }
  pos+=(unsigned)n;
 }
 assert(pos==1024);
 assert(!aster_audio_decoder_seek(d,500));assert(aster_audio_decoder_read(d,pcm,1)==1);
 uint32_t actual;memcpy(&actual,pcm,4);assert(actual==(((1000u*37+1)&(UINT32_MAX>>(32-bits)))<<(32-bits)));
 abort_read=true;assert(aster_audio_decoder_read(d,pcm,512)==-ECANCELED);
 assert(!aster_audio_decoder_close(d));assert(!allocations);
 abort_read=false;d=NULL;
 assert(aster_audio_decoder_open(argv[3],NULL,NULL,&d)<0);assert(!d && !allocations);
 return 0;
}
"""

class AudioDecoder(unittest.TestCase):
    @unittest.skipUnless(shutil.which('ffmpeg'), 'ffmpeg needed to create FLAC fixture')
    def test_large_flac_random_seek_matches_wav(self):
        with tempfile.TemporaryDirectory() as td:
            root=Path(td);wav=root/'music.wav';flac=root/'music.flac'
            rate=48000;notes=[261.6256,329.6276,391.9954,523.2511,493.8833,391.9954,440.,391.9954,329.6276,293.6648,261.6256,261.6256]
            data=bytearray()
            for i in range(rate*12):
                t=i/rate;local=t%1;f=notes[int(t)]
                envelope=min(1,local/.025)*max(0,1-local)**1.6
                note=(math.sin(2*math.pi*f*local)+.15*math.sin(4*math.pi*f*local))*.038*envelope
                pad=(math.sin(2*math.pi*130.8128*t)+.6*math.sin(2*math.pi*195.9977*t))*.006*min(1,t/.04,(12-t)/.04)
                for value in ((note+pad)*.9,(note*.78+pad)*.9):
                    data.extend((int(value*8388607)&0xffffff).to_bytes(3,'little'))
            with wave.open(str(wav),'wb') as w:
                w.setnchannels(2);w.setsampwidth(3);w.setframerate(rate);w.writeframes(data)
            subprocess.run(['ffmpeg','-v','error','-i',str(wav),'-c:a','flac',str(flac)],check=True,timeout=15)
            code=STUB[:STUB.index('static bool abort_read;')]+r"""
int main(int argc,char **argv){
 assert(argc==3);aster_audio_decoder_t *wav=NULL,*flac=NULL;int32_t a[1024],b[1024];
 assert(!aster_audio_decoder_open(argv[1],NULL,NULL,&wav));
 assert(!aster_audio_decoder_open(argv[2],NULL,NULL,&flac));
 assert(aster_audio_decoder_read(flac,b,512)==512);
 const uint64_t positions[]={240000,528000,96000,0,575500,48000,240000};
 for(unsigned i=0;i<sizeof(positions)/sizeof(positions[0]);i++){
  assert(!aster_audio_decoder_seek(wav,positions[i]));
  assert(!aster_audio_decoder_seek(flac,positions[i]));
  int n=aster_audio_decoder_read(wav,a,512),m=aster_audio_decoder_read(flac,b,512);
  assert(n>0 && n==m && !memcmp(a,b,(size_t)n*2*sizeof(int32_t)));
 }
 assert(!aster_audio_decoder_close(wav));assert(!aster_audio_decoder_close(flac));assert(!allocations);return 0;
}
"""
            (root/'test.c').write_text(code);binary=root/'test'
            subprocess.run(['cc','-std=c11','-O1','-fsanitize=address,undefined',
                '-I'+str(ROOT/'common/audio'),'-I'+str(ROOT/'common/port'),'-I'+str(ROOT/'contract/include'),
                '-I'+str(ROOT/'third_party/dr_libs'),str(root/'test.c'),str(ROOT/'common/audio/aster_audio_decoder.c'),
                str(ROOT/'common/audio/aster_audio_output.c'),'-lm','-o',str(binary)],check=True)
            subprocess.run([str(binary),str(wav),str(flac)],check=True,timeout=15)

    def test_lossless_and_cleanup(self):
        with tempfile.TemporaryDirectory() as td:
            root=Path(td);(root/'test.c').write_text(STUB)
            binary=root/'decoder'
            result=subprocess.run(['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror',
                '-Wno-unused-function','-Wno-misleading-indentation','-fsanitize=address,undefined',
                '-fno-omit-frame-pointer','-I'+str(ROOT/'common/audio'),'-I'+str(ROOT/'common/port'),
                '-I'+str(ROOT/'contract/include'),'-I'+str(ROOT/'third_party/dr_libs'),
                str(root/'test.c'),str(ROOT/'common/audio/aster_audio_decoder.c'),
                str(ROOT/'common/audio/aster_audio_output.c'),'-lm','-o',str(binary)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
            for bits in (16,24,32):
                wav=root/f'pcm{bits}.wav'
                data=b''.join(((i*37+1)&((1<<bits)-1)).to_bytes(bits//8,'little') for i in range(2048))
                with wave.open(str(wav),'wb') as w:
                    w.setnchannels(2);w.setsampwidth(bits//8);w.setframerate(48000);w.writeframes(data)
                bad=root/'truncated.wav';bad.write_bytes(wav.read_bytes()[:-7])
                subprocess.run([str(binary),str(wav),str(bits),str(bad)],check=True,timeout=10)
                if bits!=32 and shutil.which('ffmpeg'):
                    flac=root/f'pcm{bits}.flac'
                    subprocess.run(['ffmpeg','-v','error','-i',str(wav),'-c:a','flac',str(flac)],check=True,timeout=15)
                    subprocess.run([str(binary),str(flac),str(bits),str(bad)],check=True,timeout=10)

if __name__=='__main__':unittest.main()

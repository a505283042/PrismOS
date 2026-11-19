"""Compile actual streaming resampler; check rate, chunk invariance and alias rejection."""

from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class RateConvert(unittest.TestCase):
    def test_streaming_samples_and_antialias(self):
        code = r'''
#include "aster_rate_convert.h"
#include <assert.h>
#include <math.h>
#include <string.h>
#include <limits.h>
static int32_t input[96000*2];static int16_t first[144000*2],second[144000*2];
static aster_rate_convert_t a,b;
static int convert(aster_rate_convert_t *s,int16_t *out,int frames,int chunk){
 int pos=0,total=0;
 while(pos<frames){int n=frames-pos;if(n>chunk)n=chunk;int got=aster_rate_convert_process(s,input+pos*2,n,out+total*2,144000-total);assert(got>=0);total+=got;pos+=n;}
 return total;
}
int main(void){
 assert(aster_rate_convert_init(&a,0,48000)<0);
 assert(!aster_rate_convert_init(&a,48000,48000));
 input[0]=INT32_MIN;input[1]=INT32_MAX;
 assert(aster_rate_convert_process(&a,input,1,first,1)==1 && first[0]==-32768 && first[1]==32767);
 for(int i=0;i<48000;i++){input[i*2]=8192*65536;input[i*2+1]=-4096*65536;}
 assert(!aster_rate_convert_init(&a,48000,44100));b=a;
 aster_rate_convert_t saved=a;
 assert(aster_rate_convert_process(&a,input,512,first,1)<0 && !memcmp(&a,&saved,sizeof(a)));
 int n=convert(&a,first,48000,512),m=convert(&b,second,48000,17);
 assert(n==44100 && n==m && !memcmp(first,second,n*4));
 for(int i=64;i<n;i++)assert(first[i*2]==8192 && first[i*2+1]==-4096);
 /* Reject a 36 kHz component when converting 96 -> 48 kHz. */
 for(int i=0;i<96000;i++){int16_t v=(int16_t)(12000*sin(2*3.141592653589793*36000*i/96000.));input[i*2]=(int32_t)v*65536;input[i*2+1]=0;}
 assert(!aster_rate_convert_init(&a,96000,48000));n=convert(&a,first,96000,512);assert(n==48000);
 double sum=0;for(int i=64;i<n;i++){sum+=(double)first[i*2]*first[i*2];assert(!first[i*2+1]);}
 assert(sqrt(sum/(n-64))<100);
 /* Pass-band amplitude stays within 2%, independently of sample-rate family. */
 for(int i=0;i<44100;i++){int16_t v=(int16_t)(10000*sin(2*3.141592653589793*1000*i/44100.));input[i*2]=(int32_t)v*65536;input[i*2+1]=0;}
 assert(!aster_rate_convert_init(&a,44100,48000));n=convert(&a,first,44100,1);assert(n==48000);
 sum=0;for(int i=64;i<n;i++)sum+=(double)first[i*2]*first[i*2];
 double rms=sqrt(sum/(n-64));assert(rms>6900 && rms<7220);
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as folder:
            p = Path(folder)
            (p / 'test.c').write_text(code)
            subprocess.run(
                [
                    'cc',
                    '-std=c11',
                    '-Wall',
                    '-Wextra',
                    '-Werror',
                    '-fsanitize=address,undefined',
                    '-I' + str(ROOT / 'common/audio'),
                    str(p / 'test.c'),
                    str(ROOT / 'common/audio/aster_rate_convert.c'),
                    '-lm',
                    '-o',
                    str(p / 'test'),
                ],
                check=True,
                capture_output=True,
            )
            subprocess.run([str(p / 'test')], check=True, timeout=15)

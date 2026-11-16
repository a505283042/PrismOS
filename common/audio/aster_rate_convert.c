/* SPDX-License-Identifier: MIT */
#include "aster_rate_convert.h"
#include <errno.h>
#include <math.h>
#include <string.h>
int aster_rate_convert_init(aster_rate_convert_t *s,uint32_t in,uint32_t out){
 if(!s || in<16000 || in>96000 || out<32000 || out>48000)return -EINVAL;
 memset(s,0,sizeof(*s));s->input_rate=in;s->output_rate=out;
 if(in==out)return 0;
 const double pi=3.14159265358979323846;
 double cutoff=.94*(out<in?(double)out/in:1.);
 for(unsigned phase=0;phase<64;phase++){
  double values[32],sum=0;
  for(unsigned k=0;k<32;k++){
   double x=(double)k-15.-(double)phase/64.;
   double sinc=fabs(x)<1e-10?cutoff:sin(pi*cutoff*x)/(pi*x);
   double window=.42-.5*cos(2*pi*k/31.)+.08*cos(4*pi*k/31.);
   values[k]=sinc*window;sum+=values[k];
  }
  /* Q14 allows the central coefficient to represent unity without overflow. */
  int total=0;
  for(unsigned k=0;k<32;k++){
   double v=values[k]/sum*16384.;int q=(int)(v+(v<0?-.5:.5));
   s->coefficients[phase][k]=(int16_t)q;total+=q;
  }
  s->coefficients[phase][15]+=(int16_t)(16384-total);
 }
 return 0;
}
size_t aster_rate_convert_bound(const aster_rate_convert_t *s,size_t frames){
 if(!s || !s->input_rate || frames>SIZE_MAX/48000u)return SIZE_MAX;
 return ((uint64_t)frames*s->output_rate+s->phase)/s->input_rate;
}
int aster_rate_convert_process(aster_rate_convert_t *s,const int32_t *in,size_t n,int16_t *out,size_t capacity){
 if(!s || !in || !out || !s->input_rate || aster_rate_convert_bound(s,n)>capacity)return -EINVAL;
 size_t produced=0;
 if(s->input_rate==s->output_rate){
  for(size_t i=0;i<n*2;i++)out[i]=(int16_t)(in[i]>>16);
  return (int)n;
 }
 for(size_t i=0;i<n;i++){
  s->index=(s->index+1)&31;
  s->history[s->index][0]=(int16_t)(in[i*2]>>16);s->history[s->index][1]=(int16_t)(in[i*2+1]>>16);
  s->phase+=s->output_rate;
  while(s->phase>=s->input_rate){
   s->phase-=s->input_rate;
   unsigned p=(uint64_t)s->phase*64/s->output_rate;
   for(unsigned ch=0;ch<2;ch++){
    int64_t sum=0;
    for(unsigned k=0;k<32;k++)sum+=(int32_t)s->history[(s->index-k)&31][ch]*s->coefficients[p][k];
    int64_t sample=sum/16384;if(sample>32767)sample=32767;if(sample< -32768)sample=-32768;
    out[produced*2+ch]=(int16_t)sample;
   }
   produced++;
  }
 }
 return (int)produced;
}

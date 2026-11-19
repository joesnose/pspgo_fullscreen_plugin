#include "effects.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
static unsigned horizontal[272][480];
static unsigned Decode(unsigned v,int fmt){
 if(fmt==3)return v;
 if(fmt==0)return 0xff000000u|(v&31)*255/31|(((v>>5)&63)*255/63<<8)|(((v>>11)&31)*255/31<<16);
 if(fmt==1)return ((v>>15)*255<<24)|(v&31)*255/31|(((v>>5)&31)*255/31<<8)|(((v>>10)&31)*255/31<<16);
 return ((v&15)*17)|(((v>>4)&15)*17<<8)|(((v>>8)&15)*17<<16)|(((v>>12)&15)*17<<24);
}
int main(void){
 void *src=mmap((void *)0x09200000,512*272*4,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);assert(src!=MAP_FAILED);
 unsigned *dst=malloc(512*272*4);assert(dst);
 for(int fmt=0;fmt<4;fmt++){
  unsigned pixels[272][480];
  for(unsigned y=0;y<272;y++)for(unsigned x=0;x<512;x++){
   unsigned n=(x/3+y/5)%32;unsigned v=fmt==3?(0x7f000000u|n*0x070707u):fmt==2?(0x8000u|((n/2)*0x111u)):fmt==1?(0x8000u|n|(n<<5)|(n<<10)):(n|(n*2<<5)|(n<<11));
   if(fmt==3)((unsigned *)src)[y*512+x]=v;else ((unsigned short *)src)[y*512+x]=v;
   if(x<480)pixels[y][x]=Decode(v,fmt);
  }
  for(unsigned y=0;y<272;y++)for(unsigned x=0;x<480;x++)horizontal[y][x]=SmoothBandPixel(pixels[y][x?x-1:0],pixels[y][x],pixels[y][x<479?x+1:479]);
  memset(dst,0xa5,512*272*4);
  assert(DeposteriseFrame((Frame){0x09200000,512,fmt,0},dst,512)==0);
  for(unsigned y=0;y<272;y++)for(unsigned x=0;x<512;x++){
   unsigned expected=x<480?SmoothBandPixel(horizontal[y?y-1:0][x],horizontal[y][x],horizontal[y<271?y+1:271][x]):0xa5a5a5a5u;
   assert(dst[y*512+x]==expected);
  }
 }
 free(dst);assert(munmap(src,512*272*4)==0);
 unsigned short *out=malloc(TV_FRAME_BYTES);assert(out);memset(out,0xa5,TV_FRAME_BYTES);
 for(int mode=0;mode<FILTER_COUNT;mode++)DrawFilterNotice(out,TV_STRIDE,FilterName(mode));
 for(unsigned y=0;y<480;y++)for(unsigned x=0;x<TV_STRIDE;x++)if(y<12||y>=36||x<12||x>=316)assert(out[y*TV_STRIDE+x]==0xa5a5);
 assert(out[12*TV_STRIDE+12]==0x2104);free(out);
 puts("Deposterise checks passed: all source formats, independent full-frame reference, edge rows, alpha, stride padding and filter-notice bounds.");
}

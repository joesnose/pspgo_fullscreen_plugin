#include "render.h"
#include "effects.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static ScalePacket p;
int main(void){
 unsigned cases=0;
 for(int mode=0;mode<FILTER_COUNT;++mode)for(int fmt=0;fmt<4;++fmt)for(int aspect=0;aspect<2;++aspect)for(unsigned target=0;target<2;++target){
  assert(BuildFilteredScalePacket(&p,(Frame){0x09400000,512,fmt,0},target,aspect,mode)==0);
  assert(p.count<256&&p.commands[p.count-2]==0x0f000000&&p.commands[p.count-1]==0x0c000000);
  int passes=mode==FILTER_BLUR?5:mode==FILTER_SHARPEN?6:1,width=aspect?540:720,left=(720-width)/2;
  for(int pass=0;pass<passes;++pass)for(int i=0;i<15;++i){
   ScaleVertex a=p.vertices[pass*30+i*2],b=p.vertices[pass*30+i*2+1];
   assert(a.x==left+i*width/15&&b.x==left+(i+1)*width/15);
   assert(a.y==0&&b.y==480&&a.u>=0.5f&&b.u<=479.5f&&a.v>=0.5f&&b.v<=271.5f);
   if(i)assert(fabsf(a.u-p.vertices[pass*30+i*2-1].u)<0.0001f);
  }
  int texdraws=0,scandraws=0,weights=0,filter=0;unsigned weight=0;
  for(unsigned i=0;i<p.count;++i){unsigned c=p.commands[i]>>24,v=p.commands[i]&0xffffff;
   if(c==4&&v==((6u<<16)|30u))texdraws++;
   if(c==4&&v==((6u<<16)|480u))scandraws++;
   if(c==0xc6){assert(v==(mode==FILTER_SHARP?0u:0x101u));filter++;}
   if(c==0xe0)weight=v;
   if(c==0xe1&&mode==FILTER_BLUR){assert(weight+v==0xffffffu);weights++;} // constant image retains brightness
  }
  assert(filter==1&&texdraws==passes&&scandraws==(mode==FILTER_CRT||mode==FILTER_SOFT_CRT)&&weights==(mode==FILTER_BLUR?4:0));
  if(mode==FILTER_CRT)for(int i=0;i<240;++i){
   ClearVertex a=p.scanlines[i*2],b=p.scanlines[i*2+1];
   assert(a.color==0x40000000u&&b.color==a.color&&a.x==left&&b.x==left+width);
   assert(a.y==i*2+1&&b.y==i*2+2);
  }
  int dither=0,logic=0,reverse=0;
  for(unsigned i=0;i<p.count;i++){
   unsigned c=p.commands[i]>>24,v=p.commands[i]&0xffffff;
   if(c==0x20&&v==1)dither++;
   if(c==0x28&&v==1)logic++;
   if(c==0xdf&&v==0x2aa)reverse++;
  }
  assert(dither==(mode==FILTER_DITHER||mode==FILTER_SOFT_CRT));
  assert(logic==(mode==FILTER_POSTERISE));assert(reverse==(mode==FILTER_SHARPEN?4:0));
  if(mode==FILTER_RGB_MASK)for(int x=0;x<width;x++){
   assert(p.scanlines[x*2].x==left+x&&p.scanlines[x*2+1].x==left+x+1);
   assert(p.scanlines[x*2].y==0&&p.scanlines[x*2+1].y==480);
  }
  if(mode==FILTER_SOFT_CRT)assert(p.scanlines[0].color==0x20000000);
  if(mode==FILTER_POSTERISE)assert(p.post[0].color==0xffe0e0e0&&p.post[2].color==0xff242424);
  cases++;
 }
 assert(BuildFilteredScalePacket(&p,(Frame){0x09400000,512,3,0},0,0,FILTER_COUNT)<0);
 assert(SmoothBandPixel(0xff101010,0xff141414,0xff181818)==0xff141414);
 assert(SmoothBandPixel(0xff000000,0xff141414,0xffffffff)==0xff141414);
 assert(SmoothBandPixel(0xff101010,0x40141414,0xff141414)==0x40131313);
 printf("Filter checks passed: %u format/aspect/buffer/mode combinations, UV bounds, blur weights, CRT stripes and edge-aware smoothing.\n",cases);
}

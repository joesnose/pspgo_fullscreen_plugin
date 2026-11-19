#include "render.h"
#include "effects.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static ScalePacket p;
int main(void){
 // Both buffers, every pixel format and filter: LCD clip/stride/vertex bounds.
 for(int format=0;format<4;format++)for(unsigned out=0;out<2;out++)for(int f=0;f<FILTER_COUNT;f++){
  assert(BuildFilteredOutputPacket(&p,(Frame){0x09400000,512,format,0},out,0,f,1)==0);
  int saw_clip=0,saw_stride=0,saw_address=0;
  for(unsigned i=0;i<p.count;i++){
   unsigned cmd=p.commands[i]>>24,arg=p.commands[i]&0xffffffu;
   if(cmd==0x16){assert(arg==(479u|(271u<<10)));saw_clip=1;}
   if(cmd==0x9d){assert(arg==512);saw_stride=1;}
   if(cmd==0x9c){assert(arg==0x200000u+out*512u*272u*2u);saw_address=1;}
  }
  assert(saw_clip&&saw_stride&&saw_address);
  for(unsigned i=0;i<p.image_vertices;i++)assert(p.vertices[i].x>=0&&p.vertices[i].x<=480&&p.vertices[i].y>=0&&p.vertices[i].y<=272);
  for(unsigned i=0;i<p.overlay_vertices;i++)assert(p.scanlines[i].x>=0&&p.scanlines[i].x<=480&&p.scanlines[i].y>=0&&p.scanlines[i].y<=272);
  for(unsigned i=0;i<p.post_vertices;i++)assert(p.post[i].x>=0&&p.post[i].x<=480&&p.post[i].y>=0&&p.post[i].y<=272);
 }

 unsigned cases=0;
 // Building Soft must leave unused arrays untouched, not sweep the whole cache.
 memset(&p,0xa5,sizeof(p));
 assert(BuildFilteredScalePacket(&p,(Frame){0x09400000,512,3,0},0,0,FILTER_SOFT)==0);
 const unsigned char *unused=(const unsigned char *)p.scanlines;
 for(unsigned i=0;i<sizeof(p.scanlines)+sizeof(p.post)+sizeof(p.clear);i++)assert(unused[i]==0xa5);
 unused=(const unsigned char *)(p.vertices+30);
 for(unsigned i=0;i<sizeof(p.vertices)-30*sizeof(*p.vertices);i++)assert(unused[i]==0xa5);
 for(int mode=0;mode<FILTER_COUNT;++mode)for(int fmt=0;fmt<4;++fmt)for(int aspect=0;aspect<2;++aspect)for(unsigned target=0;target<2;++target){
  assert(BuildFilteredScalePacket(&p,(Frame){0x09400000,512,fmt,0},target,aspect,mode)==0);
  assert(p.count<256&&p.commands[p.count-2]==0x0f000000&&p.commands[p.count-1]==0x0c000000);
  int passes=mode==FILTER_SOFTER?4:(mode==FILTER_BLUR||mode==FILTER_BLURRIER)?5:mode==FILTER_SHARPEN?6:1,width=aspect?540:720,left=(720-width)/2;
  assert(p.image_vertices==(unsigned)passes*30u);
  assert(p.clear_vertices==(unsigned)(aspect?2:0));
  assert(p.overlay_vertices==(unsigned)(mode==FILTER_RGB_MASK?width*2:(mode==FILTER_CRT||mode==FILTER_SOFT_CRT)?480:0));
  assert(p.post_vertices==(unsigned)(mode==FILTER_POSTERISE?4:0));
  for(int pass=0;pass<passes;++pass)for(int i=0;i<15;++i){
   ScaleVertex a=p.vertices[pass*30+i*2],b=p.vertices[pass*30+i*2+1];
   assert(a.x==left+i*width/15&&b.x==left+(i+1)*width/15);
   assert(a.y==0&&b.y==480&&a.u>=0.5f&&b.u<=479.5f&&a.v>=0.5f&&b.v<=271.5f);
   if(i)assert(fabsf(a.u-p.vertices[pass*30+i*2-1].u)<0.0001f);
  }
  int clears=0,clear_off=0;
  int texdraws=0,scandraws=0,weights=0,filter=0;unsigned weight=0;
  for(unsigned i=0;i<p.count;++i){unsigned c=p.commands[i]>>24,v=p.commands[i]&0xffffff;
   if(c==0xd3&&v==0x101)clears++;
   if(c==0xd3&&v==0)clear_off++;
   if(c==4&&v==((6u<<16)|30u))texdraws++;
   if(c==4&&v==((6u<<16)|480u))scandraws++;
   if(c==0xc6){assert(v==(mode==FILTER_SHARP?0u:0x101u));filter++;}
   if(c==0xe0)weight=v;
   if(c==0xe1&&(mode==FILTER_BLUR||mode==FILTER_BLURRIER||mode==FILTER_SOFTER)){assert(weight+v==0xffffffu);weights++;} // constant image retains brightness
  }
  assert(clears==aspect&&clear_off==1);
  assert(filter==1&&texdraws==passes&&scandraws==(mode==FILTER_CRT||mode==FILTER_SOFT_CRT)&&weights==(mode==FILTER_SOFTER?3:(mode==FILTER_BLUR||mode==FILTER_BLURRIER)?4:0));
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
 Frame sample={0x09400000,512,3,0};
 BuildFilteredScalePacket(&p,sample,0,0,FILTER_BLUR);float blur_v=p.vertices[30].v;
 BuildFilteredScalePacket(&p,sample,0,0,FILTER_BLURRIER);
 assert(fabsf(p.vertices[31].v-(271.5f-1.125f))<0.0001f);
 // Interior horizontal coordinates verify the radius before edge clamping.
 BuildFilteredScalePacket(&p,sample,0,0,FILTER_BLUR);float blur_u=p.vertices[30+14].u;
 BuildFilteredScalePacket(&p,sample,0,0,FILTER_BLURRIER);float blurrier_u=p.vertices[30+14].u;
 float neutral=0.5f+224.f*(479.f/480.f);
 assert(fabsf((blurrier_u-neutral)-1.5f*(blur_u-neutral))<0.0001f);
 BuildFilteredScalePacket(&p,sample,0,0,FILTER_SOFTER);
 assert(fabsf(p.vertices[14].u-(neutral-0.5f))<0.0001f&&blur_v>=0.5f);
 assert(!strcmp(FilterName(FILTER_SOFTER),"Softer")&&!strcmp(FilterName(FILTER_BLURRIER),"Blurrier"));
 assert(SmoothBandPixel(0xff101010,0xff141414,0xff181818)==0xff141414);
 assert(SmoothBandPixel(0xff000000,0xff141414,0xffffffff)==0xff141414);
 assert(SmoothBandPixel(0xff101010,0x40141414,0xff141414)==0x40131313);
 printf("Filter checks passed: %u format/aspect/buffer/mode combinations, UV bounds, blur weights, CRT stripes and edge-aware smoothing.\n",cases);
}
